"""Put the game's own sound under the loading-screen highlight reel (Content/Movies/TAG_Highlights.mp4): the menu's
anthem over the real stadium bed, in place of the placeholder noise track. The picture is copied untouched.
Tools/Art/build_highlights.py calls this after it cuts a new reel.

  python score_reel.py [reel.mp4] [build out dir]
"""
import os, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REEL = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', '..', 'Content', 'Movies', 'TAG_Highlights.mp4')
OUT = sys.argv[2] if len(sys.argv) > 2 else os.environ.get('TAG_AUDIO_OUT', 'D:/TAGAudio/out')


def main():
    music = os.path.join(OUT, 'Music', 'MusMenu', 'MusMenu_01.wav')
    crowd = os.path.join(OUT, 'Crowd', 'CrowdBuzz', 'CrowdBuzz_01.wav')
    if not (os.path.exists(music) and os.path.exists(crowd) and os.path.exists(REEL)):
        print('score_reel: nothing to do (no built sound at %s)' % OUT)
        return
    dur = float(subprocess.check_output(['ffprobe', '-v', 'error', '-show_entries', 'format=duration', '-of', 'csv=p=0', REEL]).decode().strip())
    tmp = REEL + '.tmp.mp4'
    fc = ('[1:a]volume=0.9[m];[2:a]volume=0.35[c];[m][c]amix=inputs=2:duration=longest:normalize=0,'
          'afade=t=in:st=0:d=0.6,afade=t=out:st=%.2f:d=1.8,alimiter=limit=0.9[a]' % (dur - 1.8))
    subprocess.check_call(['ffmpeg', '-y', '-v', 'error', '-i', REEL, '-stream_loop', '-1', '-i', music, '-stream_loop', '-1', '-i', crowd,
                           '-filter_complex', fc, '-map', '0:v', '-map', '[a]', '-c:v', 'copy', '-c:a', 'aac', '-b:a', '192k', '-t', '%.3f' % dur,
                           '-movflags', '+faststart', tmp])
    shutil.move(tmp, REEL)
    print('score_reel: %s scored (%.1f s)' % (os.path.basename(REEL), dur))


if __name__ == '__main__':
    main()
