"""What a run_menu_test.sh run played and how it sits in the mix.

  <booth venv python> menu_report.py <work dir>/<name>     (reads <name>.txt and <name>.wav)

Prints, for each stretch of the walk (main, teamselect, ...): the sounds heard (bank x count); then which menu banks were never heard, and the whole run's loudness and clipping. Writes
<listen dir>/test_menus.mp3 (the menus' part of the recording) when a listen dir is given as the second argument.
"""
import os, re, subprocess, sys
from collections import Counter, OrderedDict
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import audiolib as al  # noqa: E402

SR = al.SR
KIT = ['UiMove', 'UiValue', 'UiTab', 'UiSelect', 'UiBack', 'UiError', 'UiOpen', 'UiClose', 'UiTitle', 'UiStart', 'UiTeam', 'UiSwapSides', 'UiRandom',
       'UiPick', 'UiDrop', 'UiSwap', 'UiEquip', 'UiUnequip', 'UiType', 'UiSave', 'UiDelete', 'UiJoin', 'UiLeave', 'UiCaller', 'MusMenu', 'CrowdCalm', 'CrowdBuzz']


def main():
    base = sys.argv[1]
    stages = OrderedDict()
    cur, t_last, first_t = 'boot', 0.0, None
    marks = []
    for line in open(base + '.txt', encoding='utf-8', errors='ignore'):
        m = re.match(r'TAG menuaudio (\w[\w ]*)$', line.strip())
        if m and not m.group(1).startswith(('at step', 'start')):
            cur = m.group(1)
            marks.append((cur, t_last))
            continue
        m = re.match(r'TAG audio (\d+\.\d+) (\w+) (\w+) ', line)
        if m:
            t_last = float(m.group(1))
            if first_t is None:
                first_t = t_last
            stages.setdefault(cur, Counter())[m.group(3)] += 1
    heard = Counter()
    for st, c in stages.items():
        heard.update(c)
        print('%-13s %s' % (st, '  '.join('%s x%d' % kv for kv in sorted(c.items()))))
    print('never heard :', ', '.join(k for k in KIT if k not in heard) or '(every menu bank was heard)')
    if not os.path.exists(base + '.wav'):
        return
    x = al.load(base + '.wav')
    live = np.nonzero(np.abs(x).max(axis=1) > 1e-4)[0]
    print('recording   : %.1f s, sound from %.1f to %.1f s, %.1f LUFS, peak %.1f dB, %d clipped samples'
          % (len(x) / SR, live[0] / SR if len(live) else 0, live[-1] / SR if len(live) else 0, al.lufs(x), al.peak_db(x), int((np.abs(x) > 0.999).sum())))
    if len(sys.argv) > 2 and len(live):
        out = os.path.join(sys.argv[2], 'test_menus.mp3')
        tmp = base + '_cut.wav'
        al.save(tmp, x[live[0]: live[-1]])
        subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', tmp, '-b:a', '192k', out], check=True)
        os.remove(tmp)
        print('wrote', out)


if __name__ == '__main__':
    main()
