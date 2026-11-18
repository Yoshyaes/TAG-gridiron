"""'Listen' to audio files without ears: a timeline of loudness and what a tagging model (CLAP) hears in each window.

  python listen.py <file> [file ...] [--win 5] [--labels a,b,c] [--top 3] [--whole]

Used to pick clean segments out of long field recordings (a steady murmur, a roar, a groan) and to keep anything
with music or a stadium announcer out of the build. --whole scores each file as one piece (for finished assets).
Run with HF_HOME pointing at the model cache (D:/TAGAudio/models).
"""
import argparse, os, sys, warnings
import numpy as np

warnings.filterwarnings('ignore')
os.environ.setdefault('HF_HOME', 'D:/TAGAudio/models')
os.environ['TRANSFORMERS_VERBOSITY'] = 'error'
os.environ['HF_HUB_DISABLE_PROGRESS_BARS'] = '1'
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import audiolib as al  # noqa: E402

LABELS = [
    'a large stadium crowd murmuring quietly', 'a stadium crowd cheering loudly', 'a huge crowd roaring',
    'a crowd groaning in disappointment', 'a crowd booing', 'a crowd gasping', 'a crowd chanting in rhythm',
    'a crowd applauding', 'a crowd singing a song', 'music playing', 'a man speaking over a loudspeaker',
    'a person talking nearby', 'a referee whistle', 'an air horn', 'drums', 'wind noise', 'rain',
    'a football tackle with pads colliding', 'a ball being kicked', 'footsteps running on grass', 'a man grunting',
    'silence', 'a whoosh', 'a thud', 'a metallic clang',
]

_model = None


def clap():
    global _model
    if _model is None:
        import torch
        from transformers import ClapModel, ClapProcessor
        from transformers.utils import logging
        logging.set_verbosity_error()
        logging.disable_progress_bar()
        name = 'laion/larger_clap_general'
        dev = 'cuda' if torch.cuda.is_available() else 'cpu'
        _model = (ClapModel.from_pretrained(name).to(dev).eval(), ClapProcessor.from_pretrained(name), dev, torch)
    return _model


def embed_text(labels):
    m, p, dev, torch = clap()
    with torch.no_grad():
        t = m.get_text_features(**p(text=labels, return_tensors='pt', padding=True).to(dev))
        t = t.pooler_output if hasattr(t, 'pooler_output') else t
    return torch.nn.functional.normalize(t, dim=-1)


def embed_audio(chunks):
    """chunks: list of mono float32 arrays at 48 kHz."""
    m, p, dev, torch = clap()
    out = []
    for i in range(0, len(chunks), 16):
        inp = p(audio=chunks[i:i + 16], sampling_rate=48000, return_tensors='pt', padding=True).to(dev)
        with torch.no_grad():
            a = m.get_audio_features(**inp)
            a = a.pooler_output if hasattr(a, 'pooler_output') else a
        out.append(torch.nn.functional.normalize(a, dim=-1))
    return torch.cat(out)


def score(chunks, labels):
    """Softmax over the labels per chunk -> array (chunks, labels)."""
    _, _, _, torch = clap()
    a, t = embed_audio(chunks), embed_text(labels)
    return torch.softmax(a @ t.T * 30.0, dim=-1).cpu().numpy()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('files', nargs='+')
    ap.add_argument('--win', type=float, default=5.0)
    ap.add_argument('--labels', default='')
    ap.add_argument('--top', type=int, default=3)
    ap.add_argument('--whole', action='store_true')
    a = ap.parse_args()
    labels = [s.strip() for s in a.labels.split(',')] if a.labels else LABELS
    for f in a.files:
        x = al.mono(al.load(f, mono=True))
        if a.whole or len(x) < a.win * al.SR * 1.5:
            s = score([x[: 10 * al.SR]], labels)[0]
            top = np.argsort(-s)[: a.top]
            print('%-34s %6.1fs %6.1f dB pk %5.1f | %s' % (os.path.basename(f)[:34], len(x) / al.SR, al.rms_db(x), al.peak_db(x),
                                                          '; '.join('%s %.2f' % (labels[i], s[i]) for i in top)))
            continue
        n = int(a.win * al.SR)
        chunks = [x[i:i + n] for i in range(0, len(x) - n // 2, n)]
        s = score(chunks, labels)
        print('==', os.path.basename(f), '%.1fs' % (len(x) / al.SR))
        for k, c in enumerate(chunks):
            top = np.argsort(-s[k])[: a.top]
            print('  %6.1f  %6.1f dB | %s' % (k * a.win, al.rms_db(c), '; '.join('%s %.2f' % (labels[i], s[k][i]) for i in top)))


if __name__ == '__main__':
    main()
