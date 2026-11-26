"""Build the voices that are not the booth's: quarterbacks' cadences, the defense's calls, "ball!", the stadium's PA
announcer and the referee's microphone, and the crowd's DE-FENSE chant.

Speech comes from the booth's models (Tools/Booth/.venv: Chatterbox for the shouted and announced lines, cloned from
Kokoro's stock male voices; Kokoro itself for the chant's many voices). Every take is transcribed back (Whisper) and
kept only if it says its words. Then each voice is put where it belongs: on the field (dry, a little far), on the
stadium's horns (band-limited, driven, with the bowl's slap-back), on the referee's lapel mic through those horns.

  <booth venv python> build_voices.py <out dir> [name ...]
"""
import os, re, sys, warnings
import numpy as np

warnings.filterwarnings('ignore')
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import audiolib as al  # noqa: E402

SR = al.SR
TEAMS = "Outlaws:AUS Lumberjacks:POR Vaqueros:SAT Twisters:OKC Summit:SLC Ironclads:CLB Goldrush:SAC Kings:MEM Thoroughbreds:LOU Forge:BHM Roadrunners:ABQ " \
        "Stampede:OMA Waves:HNL Kestrels:HFD Oaks:RAL Ironhorses:RIC Bison:BOI Aurora:ANC Scorpions:TUC Sundogs:ELP Timberwolves:SPO Brewmasters:MKE Archers:STL " \
        "Tritons:SDT Comets:ORL Bridges:BKN Huskies:TOR Aztecas:MEX Mariners:VAB Cyclones:DSM Palmettos:CHS Raptors:FRE".split()

QB_VOICES = ['am_adam', 'am_eric', 'am_liam', 'am_echo']
DEF_VOICES = ['am_fenrir', 'am_puck']
PA_VOICE, REF_VOICE = 'bm_george', 'am_michael'

CADENCE = {
    'Color': ['Blue eighty! Blue eighty!', 'Green nineteen! Green nineteen!', 'Red forty two! Red forty two!', 'White fifty four! White fifty four!'],
    'Set': ['Set!', 'Ready! Set!'],
    'Hut': ['Hut!', 'Set, hut!', 'Hut! Hut!'],
    'Audible': ['Kill! Kill! Kill!', 'Alert! Alert!', 'Easy! Easy! Check it!'],
}
DEF_CALLS = ['Watch the screen! Watch the screen!', 'Pass! Pass! Pass!', 'Run! Run! Run!', 'Strong right! Strong right!', 'Check! Check!', 'Motion! Motion!',
             'Watch the draw!', 'Stay home! Stay home!']
BALL_OUT = ['Ball! Ball! Ball!', 'Fumble! Fumble!', 'Ball! Get the ball!']
REF = {
    'Generic': 'Flag on the play.',
    'DelayofGame': 'Delay of game, offense. Five yard penalty.',
    'IllegalBlockintheBack': 'Illegal block in the back. Ten yard penalty.',
    'IllegalUseofHands': 'Illegal use of hands, defense. Five yard penalty. Automatic first down.',
    'TooManyMenontheField': 'Too many men on the field, defense. Five yard penalty.',
    'UnnecessaryRoughness': 'Personal foul. Unnecessary roughness, defense. Fifteen yard penalty. Automatic first down.',
    'DefensivePassInterference': 'Pass interference, defense. Automatic first down.',
    'FaceMask': 'Personal foul. Face mask. Fifteen yard penalty.',
    'FalseStart': 'False start, offense. Five yard penalty.',
    'OffensiveHolding': 'Holding, offense. Ten yard penalty.',
    'OffensivePassInterference': 'Pass interference, offense. Ten yard penalty.',
    'RoughingthePasser': 'Roughing the passer, defense. Fifteen yard penalty. Automatic first down.',
    'DefensiveHolding': 'Holding, defense. Five yard penalty. Automatic first down.',
    'IllegalContact': 'Illegal contact, defense. Five yard penalty. Automatic first down.',
    'IllegalFormation': 'Illegal formation, offense. Five yard penalty.',
    'IllegalShift': 'Illegal shift, offense. Five yard penalty.',
    'NeutralZoneInfraction': 'Neutral zone infraction, defense. Five yard penalty.',
    'Offside': 'Offside, defense. Five yard penalty.',
    'Encroachment': 'Encroachment, defense. Five yard penalty.',
    'Holding': 'Holding, on the return. Ten yard penalty.',
}
PA = {
    'PaThirdDown': ["It's third down!", 'Third down, get loud!', 'Third down!'],
    'PaMakeNoise': ['Make some noise!', 'On your feet! Make some noise!', "Let's hear it!"],
}


def spell(n):
    ones = 'zero one two three four five six seven eight nine ten eleven twelve thirteen fourteen fifteen sixteen seventeen eighteen nineteen'.split()
    tens = 'x x twenty thirty forty fifty sixty seventy eighty ninety'.split()
    if n < 20:
        return ones[n]
    if n < 100:
        return tens[n // 10] + ('' if n % 10 == 0 else ' ' + ones[n % 10])
    return str(n)


class Talker:
    def __init__(self):
        import torch
        self.dev = 'cuda' if torch.cuda.is_available() else 'cpu'
        self.kp = self.cb = self.wh = None
        self.refs = {}

    def kokoro(self, text, voice, speed=1.0):
        if self.kp is None:
            from kokoro import KPipeline
            self.kp = KPipeline(lang_code='a', device=self.dev)
        return np.concatenate([x.numpy() for _, _, x in self.kp(text, voice=voice, speed=speed)]).astype(np.float32)  # 24 kHz

    def ref(self, voice, work):
        """A reference take for cloning: the stock voice reading an energetic line."""
        if voice not in self.refs:
            import soundfile as sf
            p = os.path.join(work, 'ref_%s.wav' % voice)
            if not os.path.exists(p):
                a = self.kokoro('Here we go! Third down and four, the crowd is on its feet, and this is the biggest play of the game so far! '
                                'He takes the snap, looks left, throws deep down the sideline!', voice, 1.05)
                sf.write(p, a, 24000)
            self.refs[voice] = p
        return self.refs[voice]

    def say(self, text, voice, work, exag=0.7, cfg=0.4, takes=4, must=None):
        """Chatterbox in `voice`; the take that best says its words (None if none does)."""
        import torch
        if self.cb is None:
            from chatterbox.tts import ChatterboxTTS
            self.cb = ChatterboxTTS.from_pretrained(device=self.dev)
        if self.wh is None:
            from faster_whisper import WhisperModel
            self.wh = WhisperModel('base.en', device='cpu', compute_type='int8')
        want = re.findall(r"[a-z']+", (must or text).lower())
        best = None
        ref = self.ref(voice, work)
        for k in range(takes):
            with torch.inference_mode():
                w = self.cb.generate(text, audio_prompt_path=ref, exaggeration=exag, cfg_weight=cfg, temperature=0.75)
            a = w.squeeze().detach().cpu().numpy().astype(np.float32)
            sr = self.cb.sr
            import librosa
            a16 = librosa.resample(a, orig_sr=sr, target_sr=16000)
            segs, _ = self.wh.transcribe(a16, language='en', beam_size=1)
            said = ' '.join(s.text for s in segs).lower()
            said = re.sub(r'\d+', lambda m: ' ' + spell(int(m.group())) + ' ', said)   # Whisper writes "80", the line says "eighty"
            heard = re.findall(r"[a-z']+", said)
            hit = sum(1 for w_ in set(want) if w_ in heard) / max(1, len(set(want)))
            extra = max(0, len(heard) - len(want) - 1)
            score = hit - 0.15 * extra - (0.3 if len(a) / sr > 1.2 + 0.55 * len(want) else 0)
            if best is None or score > best[0]:
                best = (score, al.resample(librosa.resample(a, orig_sr=sr, target_sr=SR), 1.0), ' '.join(heard))
            if score >= 0.99:
                break
        return best


def tidy(a, top_db=35):
    import librosa
    _, (s, e) = librosa.effects.trim(a, top_db=top_db, frame_length=1024, hop_length=256)
    a = a[max(0, s - int(0.02 * SR)): min(len(a), e + int(0.08 * SR))]
    return al.fade(al.hp(a, 90), 0.005, 0.04)


def on_field(a, rng):
    """A man shouting forty yards from the microphone, outdoors: no room, a thinner voice, a touch of the stands."""
    a = al.hp(tidy(a), 260, 2)
    a = al.peq(a, 2200, 3, 1.0)
    a = al.compress(al.saturate(a / (np.max(np.abs(a)) + 1e-9), 1.6), -16, 3)
    wet = al.mono(al.convolve(a, al.stadium_ir(rng, rt60=1.2, slaps=((0.17, 0.2), (0.31, 0.1))), wet=0.05))
    wet = wet[: len(a) + int(0.5 * SR)]
    return al.fade((wet * (al.db(-6) / (np.max(np.abs(wet)) + 1e-9))).astype(np.float32), 0, 0.2)


def on_pa(a, rng, wet=0.5):
    x = al.pa_voice(tidy(a), rng, wet)
    x = x[: len(a) + int(2.6 * SR)]
    return al.fade(al.to_lufs(x, -18.0, -1.5), 0, 1.2)


def chant(t, work, rng, reps=4):
    """DE-FENSE, clap clap: a few hundred voices that are not quite together."""
    beat = 0.56
    period = 4 * beat
    n = int((reps * period + 2.5) * SR)
    out = np.zeros((n, 2), np.float32)
    import librosa
    voices = ['am_adam', 'am_eric', 'am_liam', 'am_echo', 'am_fenrir', 'am_puck', 'am_michael', 'am_onyx']
    syl = {}
    for v in voices:
        for word, text in (('dee', 'Dee!'), ('fence', 'Fence!')):
            a = t.kokoro(text, v, 1.0)
            a = tidy(librosa.resample(a, orig_sr=24000, target_sr=SR), 30)
            syl[(v, word)] = a / (np.max(np.abs(a)) + 1e-9)
    for _ in range(220):
        v = voices[rng.integers(len(voices))]
        ratio = rng.uniform(0.86, 1.22)   # other people: other sizes
        late = rng.normal(0.0, 0.045)
        p = rng.uniform(-1, 1)
        g = rng.uniform(0.4, 1.0)
        for r in range(reps):
            if rng.random() < 0.12:
                continue
            for word, at in (('dee', 0.0), ('fence', beat)):
                s = al.resample(syl[(v, word)], ratio)
                al.place(out, al.pan(s, p), 0.3 + r * period + at + late + rng.normal(0, 0.02), g)
    # the two claps (and the seats they stamp on)
    for _ in range(260):
        late = abs(rng.normal(0.0, 0.035))
        p = rng.uniform(-1, 1)
        k = int(0.03 * SR)
        c = al.bp(al.noise(k, rng), rng.uniform(900, 1500), rng.uniform(2500, 4500)) * np.exp(-np.arange(k) / (0.006 * SR))
        for r in range(reps):
            for at in (2 * beat, 3 * beat):
                al.place(out, al.pan(c.astype(np.float32), p), 0.3 + r * period + at + late, rng.uniform(0.3, 0.8))
    out = al.lp(al.hp(out, 140), 5200)
    out = al.convolve(out, al.stadium_ir(rng, rt60=2.4), wet=0.35, dry=0.7)[:n]
    env = np.interp(np.arange(n) / SR, [0, 1.2, reps * period, reps * period + 2.0], [0.5, 1.0, 1.0, 0.0])
    return al.to_lufs((out.T * env).T.astype(np.float32), -18.0, -1.5)


def main():
    out = sys.argv[1]
    only = sys.argv[2:]
    work = os.path.join(os.path.dirname(out.rstrip('/\\')), 'work', 'voices')
    os.makedirs(work, exist_ok=True)
    t = Talker()
    rng = np.random.default_rng(77)
    jobs = []   # (path, text, voice, place, exag, must)
    for vi, v in enumerate(QB_VOICES):
        for kind, lines in CADENCE.items():
            for k, text in enumerate(lines):
                jobs.append(('Voice/Cad%d_%s/Cad%d_%s_%02d' % (vi + 1, kind, vi + 1, kind, k + 1), text, v, 'field', 0.85, None))
    for k, text in enumerate(DEF_CALLS):
        jobs.append(('Voice/DefCall/DefCall_%02d' % (k + 1), text, DEF_VOICES[k % 2], 'field', 0.85, None))
    for k, text in enumerate(BALL_OUT):
        jobs.append(('Voice/BallOut/BallOut_%02d' % (k + 1), text, DEF_VOICES[(k + 1) % 2], 'field', 0.9, None))
    for slug, text in REF.items():
        jobs.append(('Stadium/Ref_%s/Ref_%s_01' % (slug, slug), text, REF_VOICE, 'ref', 0.45, None))
    for bank, lines in PA.items():
        for k, text in enumerate(lines):
            jobs.append(('Stadium/%s/%s_%02d' % (bank, bank, k + 1), text, PA_VOICE, 'pa', 0.8, None))
    for tm in TEAMS:
        name, ab = tm.split(':')
        jobs.append(('Stadium/PaTouchdown_%s/PaTouchdown_%s_01' % (ab, ab), 'Touchdown, %s!' % name, PA_VOICE, 'pa', 0.85, None))
        jobs.append(('Stadium/PaFirstDown_%s/PaFirstDown_%s_01' % (ab, ab), "That's another %s first down!" % name, PA_VOICE, 'pa', 0.75, None))
    bad = 0
    for path, text, voice, place, exag, must in jobs:
        if only and not any(o in path for o in only):
            continue
        dst = os.path.join(out, path + '.wav')
        if os.path.exists(dst) and not only:
            continue
        score, a, heard = t.say(text, voice, work, exag=exag, must=must)
        if score < 0.6:
            bad += 1
            print('!! %-44s %.2f "%s" heard "%s"' % (path.split('/')[-1], score, text, heard), flush=True)
            if score < 0.34:
                continue
        x = on_field(a, rng) if place == 'field' else on_pa(a, rng, 0.5 if place == 'pa' else 0.32)
        al.save(dst, x)
        print('%-46s %4.1fs %.2f "%s"' % (path.split('/')[-1], len(x) / SR, score, heard), flush=True)
    if any('Chant' in o for o in only):   # (only on request: the tagging model hears synthetic voices, not a crowd; not in the game)
        for k in range(2):
            x = chant(t, work, np.random.default_rng(900 + k))
            al.save(os.path.join(out, 'Crowd/ChantDefense/ChantDefense_%02d.wav' % (k + 1)), x)
            print('ChantDefense_%02d %.1fs' % (k + 1, len(x) / SR), flush=True)
    print('done, %d weak takes' % bad)


if __name__ == '__main__':
    main()
