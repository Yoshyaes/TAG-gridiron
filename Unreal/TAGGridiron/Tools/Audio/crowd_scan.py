"""Scan field recordings of crowds into usable windows.

Every 2 s window of every source gets: loudness, brightness, and what the tagging model hears. Windows with music, a
stadium announcer, singing, a near voice, a whistle, horns or drums are marked dirty and never reach the build
(real stadium recordings carry the house music and the PA; neither belongs in the game).

  python crowd_scan.py <src dir> <id> [id ...]      -> <src dir>/scan/<id>.npz, prints a summary line per file
"""
import os, sys
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import audiolib as al  # noqa: E402
import listen  # noqa: E402

WIN = 2.0
LABELS = ['a large crowd murmuring', 'a stadium crowd cheering loudly', 'a huge crowd roaring', 'a crowd booing',
          'a crowd groaning in disappointment', 'a crowd applauding',
          # dirty:
          'music playing', 'a crowd singing a song', 'a man speaking over a loudspeaker', 'a person talking nearby',
          'a referee whistle', 'a horn', 'drums', 'a child shouting', 'wind noise on a microphone']
DIRTY0 = 6


def scan(path):
    x = al.load(path)
    m = al.mono(x)
    n = int(WIN * al.SR)
    k = len(m) // n
    chunks = [m[i * n:(i + 1) * n] for i in range(k)]
    p = listen.score(chunks, LABELS)
    lvl = np.array([al.rms_db(c) for c in chunks])
    cen = []
    for c in chunks:
        s = np.abs(np.fft.rfft(c * np.hanning(len(c))))
        f = np.fft.rfftfreq(len(c), 1 / al.SR)
        cen.append(float((s * f).sum() / (s.sum() + 1e-9)))
    # short-term peakiness: a near clap / shout / bang stands out of a far crowd
    crest = np.array([al.peak_db(c) - al.rms_db(c) for c in chunks])
    return dict(p=p.astype(np.float32), lvl=lvl, cen=np.array(cen), crest=crest)


def main():
    src = sys.argv[1]
    os.makedirs(os.path.join(src, 'scan'), exist_ok=True)
    for i in sys.argv[2:]:
        out = os.path.join(src, 'scan', i + '.npz')
        if not os.path.exists(out):
            f = os.path.join(src, 'fs', i + '.mp3')
            if not os.path.exists(f):
                print(i, 'missing')
                continue
            np.savez(out, **scan(f))
        d = np.load(out)
        dirty = d['p'][:, DIRTY0:].sum(axis=1)
        names = ['murmur', 'cheer', 'roar', 'boo', 'groan', 'applause', 'MUSIC', 'SING', 'PA', 'TALK', 'WHISTLE', 'HORN', 'DRUMS', 'CHILD', 'WIND']
        mean = d['p'].mean(axis=0)
        top = np.argsort(-mean)[:4]
        print('%8s %4d win  clean %3d%%  lvl %5.1f..%5.1f  cen %4.0f  crest %4.1f | %s' % (
            i, len(dirty), 100 * np.mean(dirty < 0.25), np.percentile(d['lvl'], 10), np.percentile(d['lvl'], 95),
            np.median(d['cen']), np.median(d['crest']), ' '.join('%s %.2f' % (names[j], mean[j]) for j in top)))


if __name__ == '__main__':
    main()
