"""Download the picked public-domain (CC0) recordings and keep a credit sheet.

  python fetch_sources.py <src dir> <id> [id ...]      ids from candidates.json (find_sources.py)
  python fetch_sources.py <src dir> --all-in sources.txt

Each sound lands as <src dir>/fs/<id>.mp3 (freesound's high-quality preview) and gets a row in <src dir>/CREDITS.tsv
(id, title, author, licence, page). Only sounds listed under the CC0 filter are in candidates.json.
"""
import json, os, sys, time, urllib.request


def main():
    src = sys.argv[1]
    args = sys.argv[2:]
    if args and args[0] == '--all-in':
        ids = [int(t) for line in open(args[1], encoding='utf-8') for t in line.split('#')[0].split()]
    else:
        ids = [int(a) for a in args]
    cand = json.load(open(os.path.join(src, 'candidates.json'), encoding='utf-8'))
    by_id = {r['id']: r for rows in cand.values() for r in rows}
    os.makedirs(os.path.join(src, 'fs'), exist_ok=True)
    credits_path = os.path.join(src, 'CREDITS.tsv')
    have = set()
    if os.path.exists(credits_path):
        have = {int(l.split('\t')[0]) for l in open(credits_path, encoding='utf-8') if l[0].isdigit()}
    with open(credits_path, 'a', encoding='utf-8') as credits:
        for i in ids:
            r = by_id.get(i)
            if not r:
                print('not in candidates.json (not searched under the CC0 filter):', i)
                continue
            out = os.path.join(src, 'fs', '%d.mp3' % i)
            if not os.path.exists(out):
                req = urllib.request.Request(r['mp3'], headers={'User-Agent': 'Mozilla/5.0'})
                data = urllib.request.urlopen(req, timeout=120).read()
                open(out, 'wb').write(data)
                print('got', i, len(data) // 1024, 'KB', r['title'])
                time.sleep(1.0)
            if i not in have:
                credits.write('%d\t%s\t%s\tCC0 1.0\thttps://freesound.org/people/%s/sounds/%d/\n' % (i, r['title'], r['user'], r['user'], i))
                have.add(i)


if __name__ == '__main__':
    main()
