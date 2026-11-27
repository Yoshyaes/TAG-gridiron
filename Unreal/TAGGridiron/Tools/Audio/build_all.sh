#!/bin/bash
# The game's sound, from sources to assets. Each step can be run alone; every script says what it does at its top.
#
#   sources (once)   find_sources.py -> candidates.json; fetch_sources.py <ids> -> $SRC/fs/*.mp3 + CREDITS.tsv (all CC0)
#                    fetch_orchestra.py -> $SRC/vsco (VSCO-2-CE orchestral samples, CC0)
#   crowd            crowd_scan.py (clean windows of the stadium recordings) -> build_crowd.py
#   field / stadium  build_field.py (pads, ball, feet, whistles, horn, weather, the in-game graphics' sounds)
#   menus            build_ui.py (the front end: cursor, choices, screens, the title's hit, the kickoff; after build_crowd)
#   voices           build_voices.py (QB cadence, defense calls, PA announcer, referee, DE-FENSE chant)
#   music            build_music.py (broadcast theme + bumpers, halftime bed, menu playlist)
#   into the game    import_audio.py (UE python) -> /Game/TAG/Audio/<group>/<Bank>/<take>; played by Source/.../Audio/TagAudio
#   check            listen.py / events.py (what a tagging model hears), audition.py (mp3 reels), run_test.sh (a silent
#                    recorded game: -TagAudioLog -TagAudioSilent -TagAudioRec=<s>)
#
# Needs the booth's Python (Tools/Booth/.venv: torch, chatterbox, kokoro, whisper, librosa) and ffmpeg.
set -e
cd "$(dirname "$0")/../.."
SRC=${TAG_AUDIO_SRC:-D:/TAGAudio/src}
OUT=${TAG_AUDIO_OUT:-D:/TAGAudio/out}
PY=Tools/Booth/.venv/Scripts/python.exe
export HF_HOME=${HF_HOME:-D:/TAGAudio/models}
$PY Tools/Audio/crowd_scan.py $SRC 328623 648448 829456 829454 528799 691975 612131 424354 342378 324756
$PY Tools/Audio/build_crowd.py $SRC $OUT
$PY Tools/Audio/build_field.py $SRC $OUT
$PY Tools/Audio/build_ui.py $SRC $OUT
$PY Tools/Audio/build_voices.py $OUT
$PY Tools/Audio/build_music.py $SRC $OUT
"/c/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "$(cygpath -w "$PWD/TAGGridiron.uproject")" -run=pythonscript \
  -script="$(cygpath -w "$PWD/Tools/Audio/import_audio.py")" -unattended -nosplash -nullrhi
python Tools/Audio/score_reel.py
