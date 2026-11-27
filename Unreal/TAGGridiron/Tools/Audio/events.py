"""Cut a recording into its separate sounds and say what each one is (start+length / peak dB / brightness / tags).

  python events.py <src dir> <id> [id ...] [--gate -30] [--gap 0.12] [--labels a,b,c] [--max 40]
"""
import argparse, os, sys
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import audiolib as al  # noqa: E402

LABELS = ['football players colliding, shoulder pads hit', 'a man shouting', 'men talking', 'hands clapping', 'a whistle', 'a body falling on the ground',
          'a ball being kicked', 'a ball being caught', 'a ball bouncing', 'footsteps on grass', 'a man grunting', 'a whoosh', 'a metal clang',
          'an air horn', 'an explosion', 'drums', 'music', 'a click', 'wind noise', 'crowd']


def events(x, gate_db=-30.0, min_gap=0.12, pre=0.012):
    """[(start, end, peak dB)] of the sounds standing out of the recording's floor."""
    m = al.mono(x)
    n = int(0.005 * al.SR)
    k = len(m) // n
    e = 20 * np.log10(np.sqrt(np.mean(m[: k * n].reshape(k, n) ** 2, axis=1)) + 1e-9)
    thr = max(e.max() + gate_db, np.percentile(e, 20) + 8)
    on = e > thr
    out, i = [], 0
    while i < k:
        if not on[i]:
            i += 1
            continue
        j = last = i
        while j < k and (on[j] or (j - last) * 0.005 < min_gap):
            if on[j]:
                last = j
            j += 1
        out.append((max(0.0, i * 0.005 - pre), (last + 1) * 0.005, float(e[i:last + 1].max())))
        i = j
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('src')
    ap.add_argument('ids', nargs='+')
    ap.add_argument('--gate', type=float, default=-30)
    ap.add_argument('--gap', type=float, default=0.12)
    ap.add_argument('--labels', default='')
    ap.add_argument('--max', type=int, default=40)
    a = ap.parse_args()
    import listen
    labels = [s.strip() for s in a.labels.split(',')] if a.labels else LABELS
    for i in a.ids:
        f = os.path.join(a.src, 'fs', i + '.mp3')
        if not os.path.exists(f):
            print(i, 'missing')
            continue
        x = al.load(f)
        m = al.mono(x)
        ev = events(x, a.gate, a.gap)[: a.max]
        print('== %s %.1fs peak %.1f, %d events' % (i, len(m) / al.SR, al.peak_db(x), len(ev)))
        if not ev:
            continue
        chunks = [np.pad(m[int(s * al.SR): int((e + 0.25) * al.SR)], (0, al.SR // 2)) for s, e, _ in ev]
        sc = listen.score(chunks, labels)
        for (s, e, p), row in zip(ev, sc):
            top = np.argsort(-row)[:2]
            print('  %6.2f +%.2f  %5.1f dB | %s' % (s, e - s, p, '; '.join('%s %.2f' % (labels[t][:38], row[t]) for t in top)))


if __name__ == '__main__':
    main()
