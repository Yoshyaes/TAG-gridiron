"""The broadcast booth's voices: a local speech service for the game.

The game (ATagBooth) and this service talk through two folders under the cache root:
  req/<key>.json   a line to synthesize: {"key", "text", "voice": "pbp"|"color", "energy": 0..3, "prio": 0..2, "t"}
                   prio 2 = needed now (the line is up), 1 = soon (queued), 0 = background (prewarm)
  <key>.wav        the finished line (24 kHz mono 16-bit), written atomically (tmp + rename)
The game removes request files it no longer needs; the service always takes the most urgent, newest request first.
A heartbeat file (_alive) is touched every second so the game can tell a live service from a stale one.

Voices: Chatterbox Turbo cloned from two original voices (Kokoro speakers read by the full Chatterbox model at four
levels of excitement: voices/<voice>_e0..3.wav, built on first run); the line's energy picks the reference take. Every
background clip is checked with Whisper (faster-whisper) and re-rolled when the words do not match the text, so a
garbled take never reaches the cache. --engine chatterbox uses the full model (exaggeration control, ~4x slower); --engine kokoro
uses Kokoro alone (fastest, flattest).

  python booth_tts.py --cache <dir> [--engine turbo|chatterbox|kokoro] [--no-qc] [--once file.jsonl]
"""
import argparse, json, os, re, sys, time, glob, hashlib, threading, traceback
import numpy as np

os.environ.setdefault('TQDM_DISABLE', '1')  # the models' progress bars would fill the service log

HERE = os.path.dirname(os.path.abspath(__file__))
SR = 24000

# the two original voices (Kokoro speakers as the seed for the Chatterbox clones)
VOICES = {
    'pbp': dict(kokoro='am_michael', ref_text=(
        "Good evening everybody, and welcome to the stadium. It is a beautiful night for football, and we have a terrific "
        "matchup for you. Two of the best offenses in the league, and a whole lot on the line tonight.")),
    'color': dict(kokoro='am_onyx', ref_text=(
        "You know what I love about this team? They play hard for sixty minutes. They're physical up front, they run the "
        "football, and when you have to stop the run every single snap, it wears you down by the fourth quarter.")),
}
# energy 0..3 -> Chatterbox exaggeration / cfg weight (past ~0.9 the clone loses its pitch and shrieks)
EXAG = {0: (0.38, 0.5), 1: (0.5, 0.5), 2: (0.68, 0.42), 3: (0.85, 0.32)}
KOKORO_SPEED = {0: 1.0, 1: 1.05, 2: 1.12, 3: 1.18}
# what each energy's reference take reads (a voice's delivery at that energy is what Turbo copies)
ENERGY_REF = {
    0: "It's a cool, clear afternoon here, and both teams are just about ready. The captains are out at midfield for the toss.",
    1: "Second and six for the Outlaws. Holloway takes the snap, looks left, and finds Carter on the out route for seven yards.",
    2: "Holloway steps up, fires down the middle, and Carter makes the catch! He breaks a tackle, and he's into the secondary!",
    3: "He's at the thirty, the twenty, the ten, nobody's going to catch him! Touchdown! What a play by Carter! Unbelievable!",
}


def log(*a):
    print(time.strftime('%H:%M:%S'), *a, flush=True)


_ONES = ['zero', 'one', 'two', 'three', 'four', 'five', 'six', 'seven', 'eight', 'nine', 'ten', 'eleven', 'twelve',
         'thirteen', 'fourteen', 'fifteen', 'sixteen', 'seventeen', 'eighteen', 'nineteen']
_TENS = ['', '', 'twenty', 'thirty', 'forty', 'fifty', 'sixty', 'seventy', 'eighty', 'ninety']
_ORD = {'1st': 'first', '2nd': 'second', '3rd': 'third', '4th': 'fourth'}


def num_words(n):
    if n < 20:
        return _ONES[n]
    if n < 100:
        return _TENS[n // 10] + ('' if n % 10 == 0 else ' ' + _ONES[n % 10])
    if n < 1000:
        return _ONES[n // 100] + ' hundred' + ('' if n % 100 == 0 else ' ' + num_words(n % 100))
    return str(n)


def words(s):
    # what Whisper writes as digits ("33", "3:08", "2nd") is compared as the words the voice read
    s = s.lower().replace('-', ' ')
    s = re.sub(r'\b(\d+)(st|nd|rd|th)\b', lambda m: _ORD.get(m.group(0), num_words(int(m.group(1)))), s)
    s = re.sub(r'\b(\d+):(\d\d)\b', lambda m: num_words(int(m.group(1))) + ' ' + ('oh ' + num_words(int(m.group(2)))
                                                                               if int(m.group(2)) < 10 else num_words(int(m.group(2)))), s)
    s = re.sub(r'\d+', lambda m: num_words(int(m.group(0))) if len(m.group(0)) < 4 else m.group(0), s)
    return re.findall(r"[a-z']+", s)


def wer(ref, hyp):
    r, h = words(ref), words(hyp)
    if not r:
        return 0.0
    d = list(range(len(h) + 1))
    for i in range(1, len(r) + 1):
        prev, d[0] = d[0], i
        for j in range(1, len(h) + 1):
            cur = min(d[j] + 1, d[j - 1] + 1, prev + (r[i - 1] != h[j - 1]))
            prev, d[j] = d[j], cur
    return d[len(h)] / len(r)


def pid_alive(pid):
    if os.name != 'nt':
        try:
            os.kill(pid, 0)
            return True
        except OSError:
            return False
    import ctypes
    k = ctypes.windll.kernel32
    h = k.OpenProcess(0x00100000, False, int(pid))  # SYNCHRONIZE
    if not h:
        return False
    r = k.WaitForSingleObject(h, 0)
    k.CloseHandle(h)
    return r != 0  # not signaled = still running


def finish(audio, sr):
    """Trim the silence the model leaves around a line, level it to a broadcast voice level, soft edges."""
    import librosa
    a = np.asarray(audio, dtype=np.float32)
    if sr != SR:
        a = librosa.resample(a, orig_sr=sr, target_sr=SR)
    if len(a) > SR // 10:
        _, (s, e) = librosa.effects.trim(a, top_db=38, frame_length=512, hop_length=128)
        s = max(0, s - int(0.03 * SR))
        e = min(len(a), e + int(0.08 * SR))
        a = a[s:e]
    # a gentle high-pass (mic rumble) and level: speech RMS to -19 dBFS, peaks under -1 dBFS
    if len(a) > 32:
        from scipy.signal import butter, lfilter
        b, c = butter(2, 70 / (SR / 2), btype='highpass')
        a = lfilter(b, c, a).astype(np.float32)
    rms = np.sqrt(np.mean(a ** 2) + 1e-12)
    a = a * (10 ** (-19 / 20) / rms)
    peak = np.max(np.abs(a)) + 1e-9
    lim = 10 ** (-1 / 20)
    if peak > lim:
        a = np.tanh(a / lim * 1.2) * lim / np.tanh(1.2)  # soft limit instead of hard clipping
    f = min(len(a) // 4, int(0.012 * SR))
    if f > 0:
        a[:f] *= np.linspace(0, 1, f)
        a[-f:] *= np.linspace(1, 0, f)
    return a


def write_wav(path, a):
    import soundfile as sf
    tmp = path + '.tmp.wav'
    sf.write(tmp, np.clip(a, -1, 1), SR, subtype='PCM_16')
    os.replace(tmp, path)


class Engine:
    def __init__(self, kind, qc, voices_dir):
        import torch
        self.kind = kind
        self.dev = 'cuda' if torch.cuda.is_available() else 'cpu'
        self._kp = None  # Kokoro loads only when needed (references to build, a fallback, --engine kokoro)
        self.voices_dir = voices_dir
        os.makedirs(voices_dir, exist_ok=True)
        self.refs = {}
        for v, cfg in VOICES.items():
            p = os.path.join(voices_dir, f'{v}_ref.wav')
            if not os.path.exists(p):
                a = np.concatenate([x.numpy() for _, _, x in self.kp()(cfg['ref_text'], voice=cfg['kokoro'], speed=1.0)])
                write_wav(p, a)
                log('made reference voice', p)
            self.refs[v] = p
        self.cb = None
        self.tb = None
        self.conds = {}
        if kind == 'turbo':
            # Chatterbox Turbo has no exaggeration control: the energy is in the reference. Each voice has one take per
            # energy (voices/<voice>_e<n>.wav, read by the full model at that exaggeration), and Turbo clones the take
            # that matches the line. ~4x faster than the full model.
            need = [(v, e) for v in VOICES for e in range(4) if not os.path.exists(os.path.join(voices_dir, f'{v}_e{e}.wav'))]
            if need:
                from chatterbox.tts import ChatterboxTTS
                full = ChatterboxTTS.from_pretrained(device=self.dev)
                for v, e in need:
                    full.prepare_conditionals(self.refs[v], exaggeration=0.5)
                    torch.manual_seed(11 + e)
                    w = full.generate(ENERGY_REF[e], exaggeration=EXAG[e][0] + (0.03 if e == 3 else 0), cfg_weight=EXAG[e][1])
                    write_wav(os.path.join(voices_dir, f'{v}_e{e}.wav'), finish(w.squeeze().cpu().numpy(), full.sr))
                    log('made energy reference', v, e)
                del full
                torch.cuda.empty_cache()
            from chatterbox.tts_turbo import ChatterboxTurboTTS
            self.tb = ChatterboxTurboTTS.from_pretrained(device=self.dev)
            for v in VOICES:
                for e in range(4):
                    self.tb.prepare_conditionals(os.path.join(voices_dir, f'{v}_e{e}.wav'))
                    self.conds[(v, e)] = self.tb.conds
        if kind == 'chatterbox':
            from chatterbox.tts import ChatterboxTTS
            self.cb = ChatterboxTTS.from_pretrained(device=self.dev)
            for v, p in self.refs.items():
                self.cb.prepare_conditionals(p, exaggeration=0.5)
                self.conds[v] = self.cb.conds
        self.asr = None
        if qc:
            try:
                from faster_whisper import WhisperModel
                # on the CPU: the game needs the video memory (it takes ~14 GB of a 16 GB card), and QC is background work
                self.asr = WhisperModel('base.en', device='cpu', compute_type='int8', cpu_threads=4)
            except Exception as e:  # QC is a nicety: without it the first take is kept
                log('no QC (faster-whisper unavailable):', e)

    def kp(self):
        if self._kp is None:
            from kokoro import KPipeline
            self._kp = KPipeline(lang_code='a', device=self.dev)
        return self._kp

    def kokoro(self, text, voice, energy):
        cfg = VOICES[voice]
        return np.concatenate([x.numpy() for _, _, x in self.kp()(text, voice=cfg['kokoro'], speed=KOKORO_SPEED.get(energy, 1.05))]), 24000

    def chatter(self, text, voice, energy, seed):
        import torch
        if self.tb is not None:
            self.tb.conds = self.conds[(voice, max(0, min(3, energy)))]
            torch.manual_seed(seed)
            wav = self.tb.generate(text)
            return wav.squeeze().cpu().numpy(), self.tb.sr
        ex, cfgw = EXAG.get(energy, EXAG[1])
        self.cb.conds = self.conds[voice]
        torch.manual_seed(seed)
        wav = self.cb.generate(text, exaggeration=ex, cfg_weight=cfgw, temperature=0.75)
        return wav.squeeze().cpu().numpy(), self.cb.sr

    def heard(self, a):
        segs, _ = self.asr.transcribe(a.astype(np.float32), language='en', beam_size=1, vad_filter=False,
                                      condition_on_previous_text=False)
        return ' '.join(s.text for s in segs)

    def synth(self, text, voice, energy, urgent, yield_to=None):
        """One line. Background lines get up to three takes and keep the one Whisper hears best."""
        t0 = time.time()
        if self.cb is None and self.tb is None:
            a, sr = self.kokoro(text, voice, energy)
            return finish(a, sr), 'kokoro', 0.0, time.time() - t0
        best = None
        takes = 1 if (urgent or self.asr is None) else 3
        for k in range(takes):
            if k > 0 and yield_to is not None and yield_to():
                break  # a line the booth needs now is waiting: keep the best take so far
            a, sr = self.chatter(text, voice, energy, seed=1000 + k * 7919 + len(text))
            out = finish(a, sr)
            # a take far longer than the words need is a model running on (repeats, babble)
            nw = max(1, len(words(text)))
            if len(out) / SR > 1.2 + nw * 0.75:
                score = 1.0
            elif self.asr is not None and not urgent:
                import librosa
                score = wer(text, self.heard(librosa.resample(out, orig_sr=SR, target_sr=16000)))
            else:
                score = 0.0
            if best is None or score < best[1]:
                best = (out, score)
            if score <= 0.2:  # one word in five heard differently is Whisper as often as the voice ("dragged" -> "drag")
                break
        if best[1] > 0.5:
            # the clone could not say it: the plain voice can (a short exclamation trips the clone now and then)
            a, sr = self.kokoro(text, voice, energy)
            return finish(a, sr), 'kokoro-fallback', best[1], time.time() - t0
        return best[0], self.kind, best[1], time.time() - t0


def writer_thread(root, req, model, threads):
    """The booth's writer: llm_<id>.json -> booth_writer (Claude API, or a local Ollama) -> llm_<id>.txt."""
    import booth_writer
    w = booth_writer.make(model, threads)
    ready = os.path.join(root, '_llm_ready')
    while not w.ready():
        if os.path.exists(ready):
            os.remove(ready)
        time.sleep(10)
    with open(ready, 'w') as f:
        f.write(model)
    log('booth writer ready:', w.name)
    while True:
        files = sorted(glob.glob(os.path.join(req, 'llm_*.json')), key=os.path.getmtime)
        if not files:
            time.sleep(0.2)
            continue
        f = files[0]
        try:
            with open(f, encoding='utf-8') as h:
                j = json.load(h)
            os.remove(f)
        except Exception:
            time.sleep(0.1)
            continue
        t0 = time.time()
        text = booth_writer.write_safe(w, j, log=log)
        out = os.path.join(root, 'llm_%s.txt' % j['id'])
        with open(out + '.tmp', 'w', encoding='utf-8') as h:
            h.write(text)
        os.replace(out + '.tmp', out)
        log(f'writer {j.get("kind", "?")} {time.time() - t0:.1f}s')


def serve(args):
    root = os.path.abspath(args.cache)
    req = os.path.join(root, 'req')
    os.makedirs(req, exist_ok=True)
    alive = os.path.join(root, '_alive')
    # one service per cache: a second game (or a restart under load) finds the first one alive and leaves it be
    try:
        age = time.time() - os.path.getmtime(alive)
        other_pid = int(open(alive).read().strip() or 0)
    except (OSError, ValueError):
        age, other_pid = 1e9, 0
    if age < 10 and other_pid and other_pid != os.getpid() and pid_alive(other_pid):
        print('another booth service is running (pid %d): exiting' % other_pid)
        return
    if args.parent:
        # started by the game (no console): the log goes to a file beside the cache
        sys.stdout = sys.stderr = open(os.path.join(root, '_service.log'), 'a', encoding='utf-8', buffering=1)

    def parent_gone():
        # the game that started the service is gone (crashed, killed): the service goes too
        if not args.parent or os.name != 'nt':
            return False
        import ctypes
        k = ctypes.windll.kernel32
        h = k.OpenProcess(0x00100000, False, int(args.parent))  # SYNCHRONIZE
        if not h:
            return True
        r = k.WaitForSingleObject(h, 0)
        k.CloseHandle(h)
        return r == 0  # signaled = exited

    def beat():
        while True:
            with open(alive, 'w') as f:
                f.write(str(os.getpid()))
            if parent_gone():
                log('the game is gone: stopping')
                os._exit(0)
            time.sleep(1.0)
    threading.Thread(target=beat, daemon=True).start()
    if os.path.exists(os.path.join(root, '_llm_ready')):
        os.remove(os.path.join(root, '_llm_ready'))  # (a flag from an earlier service: only a running writer sets it)
    if args.writer:
        threading.Thread(target=writer_thread, args=(root, req, args.writer, args.writer_threads), daemon=True).start()
    eng = Engine(args.engine, not args.no_qc, os.path.join(HERE, 'voices'))
    with open(os.path.join(root, '_ready'), 'w') as f:
        f.write(args.engine)
    log('booth voices ready:', args.engine, 'cache', root)
    idle = 0
    rest_until = 0.0
    while True:
        files = [f for f in glob.glob(os.path.join(req, '*.json')) if not os.path.basename(f).startswith('llm_')]
        if not files:
            idle += 1
            time.sleep(0.01 if idle < 200 else 0.05)
            continue
        idle = 0
        jobs = []
        for f in files:
            try:
                with open(f, encoding='utf-8') as h:
                    j = json.load(h)
                jobs.append((int(j.get('prio', 0)), float(j.get('t', 0)), f, j))
            except Exception:
                pass  # half written: next pass
        if not jobs:
            time.sleep(0.005)
            continue
        jobs.sort(key=lambda x: (-x[0], -x[1] if x[0] >= 1 else x[1]))
        prio, _, f, j = jobs[0]
        # the game's frames come first: while a play is live only a line needed now is made, and the rest of the time
        # the service rests between lines (a speech model on the same GPU costs the game ~25% of its frame rate)
        live_flag = os.path.join(root, '_live')
        if prio < 2 and os.path.exists(live_flag) and time.time() - os.path.getmtime(live_flag) < 60:
            time.sleep(0.03)
            continue
        if prio < 2 and time.time() < rest_until:
            time.sleep(0.02)
            continue
        out = os.path.join(root, j['key'] + '.wav')
        if not os.path.exists(out):
            try:
                # only background prewarm gets the Whisper check and re-takes: a line the booth is about to say gets one take
                def urgent_waiting():
                    for g in glob.glob(os.path.join(req, '*.json')):
                        try:
                            with open(g, encoding='utf-8') as h:
                                if int(json.load(h).get('prio', 0)) >= 1:
                                    return True
                        except Exception:
                            pass
                    return False
                a, how, err, secs = eng.synth(j['text'], j.get('voice', 'pbp'), int(j.get('energy', 1)), prio >= 1,
                                              urgent_waiting if prio == 0 else None)
                write_wav(out, a)
                log(f'p{prio} {how} {secs:.2f}s wer {err:.2f} {len(a)/SR:.2f}s audio | {j["text"][:70]}')
                rest_until = time.time() + (0.0 if prio >= 2 else secs * args.rest)
            except Exception:
                traceback.print_exc()
        try:
            os.remove(f)
        except OSError:
            pass


def batch(args):
    """Synthesize every line of a BoothScript --jsonl transcript (for listening tests and the broadcast render)."""
    root = os.path.abspath(args.cache)
    os.makedirs(root, exist_ok=True)
    eng = Engine(args.engine, not args.no_qc, os.path.join(HERE, 'voices'))
    n = 0
    for line in open(args.once, encoding='utf-8'):
        j = json.loads(line)
        voice = 'pbp' if j['who'] == 'PBP' else 'color'
        key = line_key(voice, j['energy'], j['say'])
        out = os.path.join(root, key + '.wav')
        if os.path.exists(out):
            continue
        a, how, err, secs = eng.synth(j['say'], voice, int(j['energy']), False)
        write_wav(out, a)
        n += 1
        log(f'{n} {how} {secs:.2f}s wer {err:.2f} | {j["say"][:70]}')


def line_key(voice, energy, say):
    """The cache key: the same FNV-1a 64 the game computes (TagBooth.cpp LineKey)."""
    h = 0xcbf29ce484222325
    for b in f'{voice}|{energy}|{say}'.encode('utf-8'):
        h ^= b
        h = (h * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return f'{voice}{energy}_{h:016x}'


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--cache', required=True)
    ap.add_argument('--engine', default='turbo', choices=['turbo', 'chatterbox', 'kokoro'])
    ap.add_argument('--no-qc', action='store_true')
    ap.add_argument('--once', help='synthesize a BoothScript --jsonl file and exit')
    ap.add_argument('--parent', type=int, default=0, help='exit when this process (the game) exits')
    # the writer is opt-in: a 14B model on the CPU next to the game costs it half its frame rate (bench 2026-11-13: 111.7 -> 49 fps)
    ap.add_argument('--writer', default='', help="the booth writer: claude:<model> (Claude API, key from ANTHROPIC_API_KEY) or an Ollama model ('' = none)")
    ap.add_argument('--writer-threads', type=int, default=0, help='>0: run the writer on the CPU with this many threads; 0: let Ollama use the GPU')
    ap.add_argument('--rest', type=float, default=3.0, help='seconds of rest per second of synthesis between non-urgent lines (GPU share)')
    a = ap.parse_args()
    batch(a) if a.once else serve(a)
