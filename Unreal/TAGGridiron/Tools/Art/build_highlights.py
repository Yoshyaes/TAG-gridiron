# The highlight reel behind the match loading screen: Content/Movies/TAG_Highlights.mp4 (UI/LoadingScreen, played
# looping by the engine's movie player while AMatchDirector::StartMatch builds the stadium).
#   1. --capture: a CPU game filmed by the game itself (-TagHighlights: a screenshot of every frame of every live play,
#      post-whistle and instant replay at a fixed 30 fps, with the broadcast HUD) under Saved/Highlights/frames,
#      the clips logged as "TAG hl clip N end frames F big B desc ..." (and "replay from frame R").
#   2. The best plays are cut (touchdowns, takeaways, the plays the broadcast replayed, the longest gains): the last
#      seconds before the whistle and the slow-motion replay where there is one, each clip faded in and out, and the
#      reel encoded H.264 with a crowd bed under it.
#   3. --seeds 3,8,21: one capture per game seed (-TagWeatherSeed: a different game each, same dry night), frames in
#      Saved/Highlights/frames_s<seed>, logs tag_highlights_s<seed>.log; the reel picks the best plays of them all
#      (a game of 18 plays seldom has a touchdown). Without --capture the reel is re-cut from the frames on disk.
#      --matchups 3:7,0:1,6:8 films seed i with home:away pair i (-TagHome/-TagAway, league order: 0 AUS, 1 POR,
#      3 OKC, 6 SAC, 7 MEM, 8 LOU ...), so the reel shows several teams' kits; without it every game is OKC v MEM.
#   python Tools/Art/build_highlights.py [--capture] [--seeds 3,8,21] [--matchups 3:7,0:1,6:8] [--clips 8] [--secs 6]
import glob
import json
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.abspath(os.path.join(HERE, "..", ".."))
UE = r"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
FFMPEG = next((p for p in (
    r"C:\Users\soon2\AppData\Local\Microsoft\WinGet\Packages\Gyan.FFmpeg_Microsoft.Winget.Source_8wekyb3d8bbwe\ffmpeg-8.0.1-full_build\bin\ffmpeg.exe",
    "ffmpeg") if p == "ffmpeg" or os.path.exists(p)), "ffmpeg")
LOG = os.path.join(PROJ, "Saved", "Logs", "tag_highlights.log")
OUT = os.path.join(PROJ, "Content", "Movies", "TAG_Highlights.mp4")
WORK = os.path.join(PROJ, "Saved", "Highlights")
FPS = 30


def arg(name, default):
    if name in sys.argv:
        return type(default)(sys.argv[sys.argv.index(name) + 1])
    return default


FRAME_DIRS = [os.path.join(PROJ, "Saved", "Highlights", "frames"),
              os.path.join(os.path.dirname(UE), "Highlights")]  # where a relative "Highlights/" name used to land


def shots_dir():
    for d in FRAME_DIRS:
        if glob.glob(os.path.join(d, "c*.png")):
            return d
    return None


def seed_dir(seed):
    return os.path.join(WORK, "frames_s%d" % seed)


def seed_log(seed):
    return os.path.join(PROJ, "Saved", "Logs", "tag_highlights_s%d.log" % seed)


def capture(seed=None, teams=None):
    dirs = FRAME_DIRS if seed is None else [seed_dir(seed)]
    for d in dirs:
        for f in glob.glob(os.path.join(d, "c*.png")):
            os.remove(f)
    # off screen: a window that is minimised or covered stops drawing, and with it the screenshots
    cmd = [UE, os.path.join(PROJ, "TAGGridiron.uproject"), "-TagHighlights", "-game", "-RenderOffscreen", "-ResX=1280", "-ResY=720", "-ForceRes",
           "-UseFixedTimeStep", "-FPS=30", "-nosplash", "-NoLoadingScreen", "-LiveCoding=0",
           "-log=" + ("tag_highlights.log" if seed is None else os.path.basename(seed_log(seed))),
           "-ExecCmds=DisableAllScreenMessages,r.Streaming.PoolSize 3000"]  # no red debug text in the picture
    if seed is not None:
        os.makedirs(seed_dir(seed), exist_ok=True)
        cmd += ["-TagWeatherSeed=%d" % seed, "-TagHlDir=" + seed_dir(seed).replace("\\", "/")]
    if teams:
        cmd += ["-TagHome=%d" % teams[0], "-TagAway=%d" % teams[1]]
    t0 = time.time()
    subprocess.call(cmd, timeout=1800)
    print("capture%s took %.0f s" % ("" if seed is None else " (seed %d)" % seed, time.time() - t0))


def clips_from_log(log=LOG):
    clips = {}
    for line in open(log, encoding="utf-8", errors="replace"):
        m = re.search(r"TAG hl clip (\d+) end frames (\d+) big (\d+) desc \"(.*)\"", line)
        if m:
            c = clips.setdefault(int(m.group(1)), {})
            c.update(frames=int(m.group(2)), big=int(m.group(3)), desc=m.group(4))
        m = re.search(r"TAG hl clip (\d+) replay from frame (\d+)", line)
        if m:
            clips.setdefault(int(m.group(1)), {})["replay"] = int(m.group(2))
    return clips


def score(c):
    d = c.get("desc", "")
    s = 0
    # (a touchdown clip can end after the try, so its last line is the extra point / two-point try; a try filmed as
    # its own short clip is just the kick)
    tri = "extra point" in d.lower() or "two-point" in d.lower()
    if c.get("big") == 1 or "TOUCHDOWN" in d.upper() or (tri and (c.get("frames", 0) >= 8 * FPS or "replay" in c)):
        s += 100
    elif tri:
        s -= 30
    if "field goal is GOOD" in d and not tri:
        s += 30
    if c.get("big") == 2 or "INTERCEPT" in d.upper() or "FUMBLE" in d.upper():
        s += 60
    if "replay" in c:
        s += 50
    if "SACKED" in d:
        s += 30
    m = re.search(r"(\d+) yard", d)
    if m:
        s += min(40, int(m.group(1)))
    if any(w in d.lower() for w in ("kneel", "spike", "incomplete", "punts", "throws it away")) or d.startswith("PENALTY"):
        s -= 50
    return s


# share of the picture that is one smooth, defocused blob: a body on the lens. The close shots are filmed at f/1.2
# since 16 Nov, so a clean close-up with the crowd melted behind it reads up to ~0.46; a body on the lens 0.6+
BLOCK_FLAT = 0.5
BLOCK_PAD = 6      # frames trimmed either side of a blocked span (the body coming into and leaving the frame)


def frame_feats(path):
    """(flatness, small grey image) of one frame, above the HUD strip."""
    import numpy as np
    from PIL import Image
    from numpy.lib.stride_tricks import sliding_window_view
    im = np.asarray(Image.open(path).convert("L").resize((160, 90), Image.BILINEAR), dtype=np.float32)[:76]
    g = np.abs(np.diff(im, axis=1))[:-1] + np.abs(np.diff(im, axis=0))[:, :-1]
    flat = float((sliding_window_view(g, (5, 5)).max(axis=(2, 3)) < 4.0).mean())
    return flat, im


def clean_frames(files, replay=None):
    """Indices of the clip's frames to use: the post-whistle close-up sits at field level and players walk across the
    lens. Cut the clip into shots (a camera cut is a lone spike in frame difference: close-up motion runs high frame
    after frame), split each shot around its blocked frames and keep only its longest clean run, so a shot is never
    shown with a jump in time."""
    import numpy as np
    from concurrent.futures import ThreadPoolExecutor
    with ThreadPoolExecutor(8) as ex:
        feats = list(ex.map(frame_feats, files))
    flat = [f for f, _ in feats]
    diff = [0.0] + [float(np.abs(feats[i][1] - feats[i - 1][1]).mean()) for i in range(1, len(feats))]
    cuts = [0]
    for i in range(1, len(diff)):
        nb = max(diff[i - 1] if i > 1 else 0.0, diff[i + 1] if i + 1 < len(diff) else 0.0)
        if diff[i] > 40 and diff[i] > 2 * nb:
            cuts.append(i)
    if replay is not None and 0 < replay < len(files):
        cuts.append(replay)   # (the log knows this cut for certain)
    cuts = sorted(set(cuts)) + [len(files)]
    bad = set()
    for i, f in enumerate(flat):
        if f > BLOCK_FLAT:
            bad.update(range(i - BLOCK_PAD, i + BLOCK_PAD + 1))
    keep, dropped = [], 0
    for a, b in zip(cuts, cuts[1:]):
        runs, cur = [], []
        for i in range(a, b):
            if i in bad:
                if cur: runs.append(cur)
                cur = []
            else:
                cur.append(i)
        if cur: runs.append(cur)
        # a shot mostly blocked is a bad camera (the replay camera behind an end zone sits in the stands): all of it goes
        nbad = sum(1 for i in range(a, b) if i in bad)
        best = max(runs, key=len) if runs and nbad <= 0.4 * (b - a) else []
        if len(best) >= 12:
            keep += best
        dropped += (b - a) - (len(best) if len(best) >= 12 else 0)
    return keep, dropped


def sources():
    """(seed, frames dir, log) for every capture on disk: the seeded ones, else the single legacy capture."""
    out = []
    for d in sorted(glob.glob(os.path.join(WORK, "frames_s*"))):
        seed = int(re.search(r"frames_s(\d+)$", d).group(1))
        if glob.glob(os.path.join(d, "c*.png")) and os.path.exists(seed_log(seed)):
            out.append((seed, d, seed_log(seed)))
    if not out and shots_dir():
        out.append((0, shots_dir(), LOG))
    return out


def main():
    seeds = [int(x) for x in arg("--seeds", "").split(",") if x.strip()]
    if "--capture" in sys.argv:
        if seeds:
            # the old frames show the old bodies: a seeded reel never mixes them in
            for old in glob.glob(os.path.join(WORK, "frames_s*")):
                if int(re.search(r"frames_s(\d+)$", old).group(1)) not in seeds:
                    for f in glob.glob(os.path.join(old, "c*.png")):
                        os.remove(f)
            for d in FRAME_DIRS:
                for f in glob.glob(os.path.join(d, "c*.png")):
                    os.remove(f)
            pairs = [tuple(int(t) for t in m.split(":")) for m in arg("--matchups", "").split(",") if m.strip()]
            for i, sd in enumerate(seeds):
                capture(sd, pairs[i] if i < len(pairs) else None)
        else:
            capture()
    n_clips = arg("--clips", 8)
    secs = arg("--secs", 6.0)
    src = sources()
    if not src:
        raise SystemExit("no Highlights screenshots; run with --capture")
    clips = {}
    for seed, d, log in src:
        for k, c in clips_from_log(log).items():
            c["files"] = sorted(glob.glob(os.path.join(d, "c%02d_*.png" % k)))
            c["seed"] = seed
            clips[(seed, k)] = c
    good = [k for k, c in clips.items() if len(c["files"]) >= FPS * 2]
    good.sort(key=lambda k: -score(clips[k]))
    # one kickoff at most: they all look alike from the broadcast camera; and no game more than its share, so every
    # matchup filmed (its teams' kits) is in the reel
    seen_kick = False
    pick = []
    per_seed = -(-n_clips // max(1, len(src)))
    for k in good:
        if len(pick) >= n_clips:
            break
        kick = " kicks " in clips[k].get("desc", "")
        if kick and seen_kick:
            continue
        if sum(1 for p in pick if p[0] == k[0]) >= per_seed:
            continue
        # frames with a body on the lens are cut; a play left with under 4 s of picture is passed over
        keep, dropped = clean_frames(clips[k]["files"], clips[k].get("replay"))
        if k[1] == 1:
            # the game's first play opens under the start-up controller toast ("No PlayStation controller found", 3.2 s)
            keep = [j for j in keep if j >= int(3.4 * FPS)]
        if len(keep) < FPS * 4:   # (under 4 s of picture reads as a flash, not a play)
            print("skip seed %d clip %d: %d clean frames" % (k[0], k[1], len(keep)))
            continue
        clips[k]["keep"], clips[k]["dropped"] = keep, dropped
        seen_kick = seen_kick or kick
        pick.append(k)
    # the reel plays the best play last, so each loop builds to it
    pick.sort(key=lambda k: score(clips[k]))
    os.makedirs(WORK, exist_ok=True)
    parts = []
    for i, k in enumerate(pick):
        c = clips[k]
        files = c["files"]
        keep = c["keep"]
        n = int(secs * FPS * (1.4 if score(c) >= 100 else 1.0))   # the big plays get a little longer
        rep = [files[j] for j in keep if "replay" in c and j >= c["replay"]]
        live_all = [files[j] for j in keep if "replay" not in c or j < c["replay"]]
        if len(rep) >= FPS * 1.5:
            live = live_all[-int(2.5 * FPS):]                                   # the live finish...
            sel = live + rep[-max(FPS * 3, n - len(live)):] if len(rep) > n - len(live) else live + rep   # ...then the slow-motion look at it
        else:
            sel = live_all[-n:]   # no replay, or none left after the blocked shots: the play itself
        lst = os.path.join(WORK, "clip%02d.txt" % i)
        with open(lst, "w", encoding="utf-8") as f:
            for p in sel:
                f.write("file '%s'\nduration %.5f\n" % (p.replace("\\", "/"), 1.0 / FPS))
            f.write("file '%s'\n" % sel[-1].replace("\\", "/"))
        out = os.path.join(WORK, "clip%02d.mp4" % i)
        dur = len(sel) / FPS
        subprocess.check_call([FFMPEG, "-y", "-loglevel", "error", "-f", "concat", "-safe", "0", "-i", lst,
                               "-vf", "fade=t=in:st=0:d=0.35,fade=t=out:st=%.3f:d=0.35,scale=1280:720,format=yuv420p" % (dur - 0.35),
                               "-r", str(FPS), "-c:v", "libx264", "-crf", "18", "-preset", "medium", out])
        parts.append((out, dur))
        print("seed %d clip %d: %s  (%d frames, score %d, %d blocked/cut away)%s" % (k[0], k[1], c.get("desc", "")[:70], len(sel), score(c), c["dropped"], "  +replay" if "replay" in c else ""))
    total = sum(p[1] for p in parts)
    cat = os.path.join(WORK, "reel.txt")
    with open(cat, "w", encoding="utf-8") as f:
        for p, _ in parts:
            f.write("file '%s'\n" % p.replace("\\", "/"))
    # a crowd bed: low pink noise breathing slowly, rising a little into each clip's end
    bed = ("anoisesrc=color=pink:seed=11:amplitude=0.5:d=%.2f,lowpass=f=520,highpass=f=90,"
           "volume='0.10+0.04*sin(2*PI*t/7.3)':eval=frame,afade=t=in:st=0:d=1.5,afade=t=out:st=%.2f:d=1.5" % (total, total - 1.5))
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    subprocess.check_call([FFMPEG, "-y", "-loglevel", "error", "-f", "concat", "-safe", "0", "-i", cat, "-f", "lavfi", "-i", bed,
                           "-c:v", "libx264", "-crf", "22", "-preset", "slow", "-pix_fmt", "yuv420p", "-profile:v", "high", "-level", "4.1",
                           "-c:a", "aac", "-b:a", "128k", "-movflags", "+faststart", "-shortest", OUT])
    json.dump({"clips": [{"seed": k[0], "n": k[1], "desc": clips[k].get("desc"), "score": score(clips[k])} for k in pick], "seconds": total},
              open(os.path.join(WORK, "reel.json"), "w"), indent=1)
    print("TAG_HIGHLIGHTS wrote %s: %d clips, %.1f s, %d KB" % (OUT, len(parts), total, os.path.getsize(OUT) // 1024))


if __name__ == "__main__":
    main()
