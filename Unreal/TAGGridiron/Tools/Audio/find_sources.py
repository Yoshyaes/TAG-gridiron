"""Find public-domain (CC0) source recordings for the game's sound set.

Searches freesound.org with the "Creative Commons 0" licence filter only and writes the candidates per category to
<out>/candidates.json (id, title, user, duration, downloads, preview url). Nothing is downloaded here: fetch_sources.py
takes the ids picked in sources.json. One request per 1.5 s.

  python find_sources.py <out dir> [category ...]
"""
import html, json, os, re, sys, time, urllib.parse, urllib.request

QUERIES = {
    'crowd_bed':      ['stadium crowd ambience', 'football stadium crowd', 'sports crowd murmur', 'arena crowd ambience', 'large crowd stadium walla'],
    'crowd_cheer':    ['stadium crowd cheer', 'crowd roar goal', 'crowd cheer touchdown', 'big crowd cheering applause', 'crowd erupts'],
    'crowd_groan':    ['crowd groan', 'crowd aww disappointed', 'crowd ohh miss', 'stadium crowd disappointment'],
    'crowd_boo':      ['crowd boo', 'stadium booing'],
    'crowd_gasp':     ['crowd gasp', 'crowd ooh', 'crowd reaction oh'],
    'crowd_chant':    ['crowd chant clap', 'crowd rhythmic clapping stadium', 'crowd stomp clap'],
    'crowd_applause': ['stadium applause', 'large crowd applause outdoor'],
    'pads':           ['football pads hit', 'football tackle', 'shoulder pads', 'football helmet hit', 'american football hit', 'body impact pads'],
    'bodyfall':       ['body fall grass', 'bodyfall dirt', 'body thud ground', 'tackle fall grass'],
    'kick':           ['football kick', 'punt football', 'kick ball thud', 'soccer ball kick'],
    'catch':          ['football catch', 'catch ball hands', 'ball catch leather', 'ball slap hands'],
    'throw':          ['whoosh pass', 'throw whoosh', 'swish air fast'],
    'bounce':         ['football bounce grass', 'ball bounce grass', 'ball drop grass'],
    'whistle':        ['referee whistle', 'sports whistle', 'coach whistle blow', 'fox 40 whistle'],
    'footsteps':      ['running grass footsteps', 'footsteps grass run', 'cleats grass', 'sprint grass'],
    'grunt':          ['male grunt effort', 'man grunt hit', 'athlete grunt', 'male exertion'],
    'post':           ['metal pole hit', 'metal pipe clang', 'goalpost hit'],
    'airhorn':        ['air horn stadium', 'airhorn', 'stadium horn'],
    'fireworks':      ['fireworks burst', 'cannon shot', 'firework mortar'],
    'drumline':       ['marching band drumline', 'drumline cadence', 'marching snare cadence', 'marching bass drum'],
    'rain':           ['rain on stadium', 'steady rain outdoor', 'heavy rain ambience'],
    'wind':           ['wind open field', 'wind gusts outdoor'],
    'thunder':        ['thunder distant rumble'],
    'ui':             ['ui click', 'menu select', 'interface whoosh'],
    'whoosh':         ['whoosh transition', 'swoosh broadcast', 'cinematic whoosh'],
    'coach':          ['football coach yelling', 'football practice', 'football players shouting', 'quarterback cadence'],
    'pa':             ['stadium announcer pa', 'public address stadium'],
    'organ':          ['stadium organ', 'ballpark organ'],
    'cowbell':        ['cowbell crowd', 'vuvuzela', 'crowd whistle fan', 'thundersticks'],
}

ITEM = re.compile(r'class="bw-player"(.*?)tabindex', re.S)
ATTR = re.compile(r'data-([a-z0-9-]+)="([^"]*)"')


def search(q, page=1):
    url = 'https://freesound.org/search/?' + urllib.parse.urlencode({
        'q': q, 'f': 'license:"Creative Commons 0"', 's': 'Downloads (most first)', 'page': page})
    req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
    page_html = urllib.request.urlopen(req, timeout=40).read().decode('utf-8', 'replace')
    out = []
    for m in ITEM.finditer(page_html):
        a = dict(ATTR.findall(m.group(1)))
        if 'sound-id' not in a:
            continue
        out.append({'id': int(a['sound-id']), 'user': a.get('username', ''), 'title': html.unescape(a.get('title', '')),
                    'dur': round(float(a.get('duration', 0)), 2), 'sr': int(float(a.get('samplerate', 0))),
                    'dl': int(a.get('num-downloads', 0) or 0), 'mp3': a.get('mp3', '').replace('-lq.', '-hq.')})
    return out


def main():
    out = sys.argv[1]
    cats = sys.argv[2:] or list(QUERIES)
    os.makedirs(out, exist_ok=True)
    path = os.path.join(out, 'candidates.json')
    found = json.load(open(path, encoding='utf-8')) if os.path.exists(path) else {}
    for cat in cats:
        seen, rows = set(), []
        for q in QUERIES[cat]:
            for page in (1, 2):
                try:
                    res = search(q, page)
                except Exception as e:  # noqa: BLE001
                    print('  !', q, page, e)
                    res = []
                for r in res:
                    if r['id'] not in seen:
                        seen.add(r['id'])
                        r['q'] = q
                        rows.append(r)
                time.sleep(1.5)
                if len(res) < 15:
                    break
        found[cat] = rows
        print(cat, len(rows))
        json.dump(found, open(path, 'w', encoding='utf-8'), indent=1)


if __name__ == '__main__':
    main()
