# The highlight reel behind the match loading screen: Content/Movies/TAG_Highlights.mp4 (UI/LoadingScreen, played
# looping by the engine's movie player while AMatchDirector::StartMatch builds the stadium).
#   1. --capture: a CPU game filmed by the game itself (-TagHighlights: a screenshot of every frame of every live play,
#      post-whistle and instant replay at a fixed 30 fps, with the broadcast HUD) under Saved/Highlights/frames,
#      the clips logged as "TAG hl clip N end frames F big B desc ..." (and "replay from frame R").
#   2. The best plays are cut (touchdowns, takeaways, the plays the broadcast replayed, the longest gains): the last
#      seconds before the whistle and the slow-motion replay where there is one, each clip faded in and out, and the
#      reel encoded H.264 with a crowd bed under it.
#   python Tools/Art/build_highlights.py [--capture] [--clips 8] [--secs 6]
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


def capture():
    for d in FRAME_DIRS:
        for f in glob.glob(os.path.join(d, "c*.png")):
            os.remove(f)
    # off screen: a window that is minimised or covered stops drawing, and with it the screenshots
    cmd = [UE, os.path.join(PROJ, "TAGGridiron.uproject"), "-TagHighlights", "-game", "-RenderOffscreen", "-ResX=1280", "-ResY=720", "-ForceRes",
           "-UseFixedTimeStep", "-FPS=30", "-nosplash", "-NoLoadingScreen", "-LiveCoding=0", "-log=tag_highlights.log",
           "-ExecCmds=DisableAllScreenMessages,r.Streaming.PoolSize 3000"]  # no red debug text in the picture
    t0 = time.time()
    subprocess.call(cmd, timeout=1800)
    print("capture took %.0f s" % (time.time() - t0))


def clips_from_log():
    clips = {}
    for line in open(LOG, encoding="utf-8", errors="replace"):
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
    if c.get("big") == 1 or "TOUCHDOWN" in d.upper():
        s += 100
    if c.get("big") == 2 or "INTERCEPT" in d.upper() or "FUMBLE" in d.upper():
        s += 60
    if "replay" in c:
        s += 50
    m = re.search(r"(\d+) yard", d)
    if m:
        s += min(40, int(m.group(1)))
    if "kneel" in d.lower() or "spike" in d.lower() or "incomplete" in d.lower() or "punts" in d.lower():
        s -= 50
    return s


def main():
    if "--capture" in sys.argv:
        capture()
    n_clips = arg("--clips", 8)
    secs = arg("--secs", 6.0)
    d = shots_dir()
    if not d:
        raise SystemExit("no Highlights screenshots; run with --capture")
    clips = clips_from_log()
    for k, c in clips.items():
        c["files"] = sorted(glob.glob(os.path.join(d, "c%02d_*.png" % k)))
    good = [k for k, c in clips.items() if len(c["files"]) >= FPS * 2]
    good.sort(key=lambda k: -score(clips[k]))
    pick = sorted(good[:n_clips])
    os.makedirs(WORK, exist_ok=True)
    parts = []
    for i, k in enumerate(pick):
        c = clips[k]
        files = c["files"]
        n = int(secs * FPS)
        if "replay" in c and c["replay"] < len(files) - FPS:
            live = files[max(0, c["replay"] - int(2.5 * FPS)):c["replay"]]   # the live finish...
            rep = files[c["replay"]:]                                           # ...then the slow-motion look at it
            sel = live + rep[-max(FPS * 3, n - len(live)):] if len(rep) > n - len(live) else live + rep
        else:
            sel = files[-n:]
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
        print("clip %d: %s  (%d frames, score %d)%s" % (k, c.get("desc", "")[:70], len(sel), score(c), "  +replay" if "replay" in c else ""))
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
    json.dump({"clips": [{"n": k, "desc": clips[k].get("desc"), "score": score(clips[k])} for k in pick], "seconds": total},
              open(os.path.join(WORK, "reel.json"), "w"), indent=1)
    print("TAG_HIGHLIGHTS wrote %s: %d clips, %.1f s, %d KB" % (OUT, len(parts), total, os.path.getsize(OUT) // 1024))


if __name__ == "__main__":
    main()
