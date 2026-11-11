# The game's startup movie: Content/Movies/TAG_Intro.mp4 (played by the engine's movie player before the main menu,
# Config/DefaultGame.ini [MoviePlayerSettings]). Renders the brand sting with Blender (Tools/Art/intro_render.py),
# gives it a bloom, a fade in and out, and a sound (a swell into a hit as the word lands, a soft pad under the
# sub-title), and encodes H.264 for Windows Media Foundation.
#   python Tools/Art/build_intro.py [--no-render]
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.abspath(os.path.join(HERE, "..", ".."))
FRAMES = os.path.join(PROJ, "Saved", "Intro", "frames")
OUT = os.path.join(PROJ, "Content", "Movies", "TAG_Intro.mp4")
BLENDER = r"C:\Program Files\Blender Foundation\Blender 5.2\blender.exe"
FFMPEG = next((p for p in (
    r"C:\Users\soon2\AppData\Local\Microsoft\WinGet\Packages\Gyan.FFmpeg_Microsoft.Winget.Source_8wekyb3d8bbwe\ffmpeg-8.0.1-full_build\bin\ffmpeg.exe",
    "ffmpeg") if p == "ffmpeg" or os.path.exists(p)), "ffmpeg")
FPS = 30
N = 150
LAND = 37 / FPS  # the word lands (the gold flash) here


def main():
    if "--no-render" not in sys.argv:
        env = dict(os.environ, TAG_INTRO_NOCOMP="1")
        subprocess.check_call([BLENDER, "-b", "-P", os.path.join(HERE, "intro_render.py"), "--", FRAMES, str(N)], env=env)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    # sound: a low swell rising into the landing, a hit (a sub thump with a short bright click) at the landing, a soft
    # two-note pad under the sub-title, everything gone by the fade
    swell = ("anoisesrc=color=brown:seed=3:amplitude=0.9:d=5,lowpass=f=700,"
             "volume='if(lt(t,%.3f), pow(t/%.3f,2.2)*0.9, 0.9*exp(-(t-%.3f)*6))':eval=frame" % (LAND, LAND, LAND))
    thump = "sine=frequency=48:duration=5,volume='if(lt(t,%.3f),0,exp(-(t-%.3f)*2.2))':eval=frame" % (LAND, LAND)
    click = ("anoisesrc=color=white:seed=5:amplitude=0.6:d=5,highpass=f=1800,"
             "volume='if(lt(t,%.3f),0,exp(-(t-%.3f)*40))':eval=frame" % (LAND, LAND))
    pad = ("sine=frequency=220:duration=5,sine=frequency=330:duration=5" if False else
           "aevalsrc='0.5*sin(2*PI*220*t)+0.35*sin(2*PI*330*t)+0.2*sin(2*PI*440*t)':s=48000:d=5,"
           "volume='if(lt(t,1.5),0,if(lt(t,2.4),(t-1.5)/0.9,1))*0.16':eval=frame")
    afilter = ("[0]%s[a];[1]%s[b];[2]%s[c];[3]%s[d];[a][b][c][d]amix=inputs=4:normalize=0,"
               "afade=t=out:st=4.1:d=0.85,alimiter=limit=0.9[aout]" % (
                   swell.split(",", 1)[1], thump.split(",", 1)[1], click.split(",", 1)[1], pad.split(",", 1)[1]))
    srcs = []
    for spec in (swell, thump, click, pad):
        srcs += ["-f", "lavfi", "-i", spec.split(",", 1)[0]]
    # picture: a bloom on the bright parts (the gold's glint, the light banks), a fade in and out
    # (planar RGB throughout: packed RGB into blend shifts the hues in ffmpeg 8)
    vfilter = ("[4:v]format=gbrp,split[base][glow];[glow]colorlevels=rimin=0.5:gimin=0.5:bimin=0.5,gblur=sigma=30,format=gbrp[glowb];"
               "[base][glowb]blend=all_mode=screen:all_opacity=0.65,fade=t=in:st=0:d=0.5,fade=t=out:st=%.2f:d=0.7,format=yuv420p[vout]"
               % ((N - 21) / FPS))
    cmd = [FFMPEG, "-y", *srcs, "-framerate", str(FPS), "-i", os.path.join(FRAMES, "frame_%04d.png"),
           "-filter_complex", afilter + ";" + vfilter, "-map", "[vout]", "-map", "[aout]",
           "-c:v", "libx264", "-profile:v", "high", "-level", "4.1", "-crf", "17", "-preset", "slow", "-r", str(FPS),
           "-c:a", "aac", "-b:a", "192k", "-movflags", "+faststart", "-shortest", OUT]
    subprocess.check_call(cmd)
    print("TAG_INTRO wrote", OUT, os.path.getsize(OUT) // 1024, "KB")


if __name__ == "__main__":
    main()
