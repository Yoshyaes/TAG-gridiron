"""An audition reel of the built sounds: every bank in turn, a take or two of each, with gaps, as one mp3 per group
(crowd, field, voices, stadium, music, ui), so the whole set can be listened through in a few minutes.

  python audition.py <out dir of the build> <listen dir>
"""
import glob, os, subprocess, sys
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import audiolib as al  # noqa: E402

SR = al.SR
GROUPS = {
    'crowd': ('Crowd', 2, 9.0), 'field': ('Field', 3, 2.0), 'voices': ('Voice', 2, 4.0), 'stadium': ('Stadium', 1, 9.0), 'weather': ('Weather', 1, 8.0),
    'music': ('Music', 9, 400.0), 'ui': ('Ui', 1, 2.0),
}


def main():
    out, listen = sys.argv[1], sys.argv[2]
    os.makedirs(listen, exist_ok=True)
    for name, (folder, takes, max_secs) in GROUPS.items():
        parts, index, t = [], [], 0.0
        banks = sorted(glob.glob(os.path.join(out, folder, '*')))
        if folder == 'Stadium':
            # one team's PA lines stand for all thirty-two
            banks = [b for b in banks if not (os.path.basename(b).startswith(('PaTouchdown_', 'PaFirstDown_')) and not b.endswith('_OKC'))]
        for b in banks:
            for f in sorted(glob.glob(os.path.join(b, '*.wav')))[:takes]:
                x = al.load(f)[: int(max_secs * SR)]
                x = al.fade(x, 0, min(0.5, len(x) / SR / 4))
                index.append('%6.1f  %s' % (t, os.path.basename(f)[:-4]))
                parts += [x, al.silence(0.45)]
                t += len(x) / SR + 0.45
            parts.append(al.silence(0.5))
            t += 0.5
        if not parts:
            continue
        wav = os.path.join(listen, '_tmp.wav')
        al.save(wav, np.concatenate(parts))
        mp3 = os.path.join(listen, 'audition_%s.mp3' % name)
        subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', wav, '-b:a', '192k', mp3], check=True)
        os.remove(wav)
        open(os.path.join(listen, 'audition_%s.txt' % name), 'w').write('\n'.join(index) + '\n')
        print('%-10s %5.0f s, %d sounds' % (name, t, len(index)))


if __name__ == '__main__':
    main()
