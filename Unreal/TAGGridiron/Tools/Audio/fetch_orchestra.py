"""Download the orchestral samples the music is played on: Versilian Studios Chamber Orchestra 2 Community Edition
(VSCO-2-CE, github.com/sgossner/VSCO-2-CE), released under CC0 1.0 (public domain). Only the instruments and
articulations the scores use are fetched (about 0.7 GB) into <dir>/vsco/<same path as the repository>.

  python fetch_orchestra.py <dir>
"""
import json, os, sys, time, urllib.parse, urllib.request

WANT = [
    ('Brass/Trumpet/sus/', None), ('Brass/Trumpet/stac/', None),
    ('Brass/F Horn/sus/', None), ('Brass/F Horn/stac/', None),
    ('Brass/Tenor Trombone/sus/', None), ('Brass/Tenor Trombone/stac/', None),
    ('Brass/Tuba/sus/', None), ('Brass/Tuba/stac/', None),
    ('Strings/Violin Section/susVib/', None), ('Strings/Violin Section/Spic/', None),
    ('Strings/Viola Section/susvib/', None), ('Strings/Viola Section/spic/', None),
    ('Strings/Cello Section/susvib/', None), ('Strings/Cello Section/spic/', None),
    ('Percussion/Timpani/', None),
    ('Percussion/', ('Snare2-', 'BDrumNewhit', 'cymbal-crash', 'susCymb1-cresc', 'susCymb1-hit_', 'gongHit', 'Tamb1-', 'timpsnare')),
    ('Woodwinds/Piccolo/', None),
]


def main():
    root = os.path.join(sys.argv[1], 'vsco')
    os.makedirs(root, exist_ok=True)
    tree_file = os.path.join(root, 'tree.json')
    if not os.path.exists(tree_file):
        data = urllib.request.urlopen('https://api.github.com/repos/sgossner/VSCO-2-CE/git/trees/master?recursive=1', timeout=60).read()
        open(tree_file, 'wb').write(data)
    tree = json.load(open(tree_file))['tree']
    n = 0
    for e in tree:
        p = e['path']
        if e['type'] != 'blob' or not p.lower().endswith('.wav'):
            continue
        ok = False
        for pre, names in WANT:
            if p.startswith(pre) and (names is None or ('/' not in p[len(pre):] and p[len(pre):].startswith(names))):
                ok = True
        if not ok:
            continue
        dst = os.path.join(root, p)
        if os.path.exists(dst) and os.path.getsize(dst) == e.get('size', -1):
            continue
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        url = 'https://raw.githubusercontent.com/sgossner/VSCO-2-CE/master/' + urllib.parse.quote(p)
        for attempt in range(3):
            try:
                data = urllib.request.urlopen(url, timeout=120).read()
                open(dst, 'wb').write(data)
                n += 1
                break
            except Exception as ex:  # noqa: BLE001
                print('retry', p, ex, flush=True)
                time.sleep(3)
    print('fetched', n, flush=True)


if __name__ == '__main__':
    main()
