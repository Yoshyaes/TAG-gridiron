#!/bin/bash
# A silent, offscreen CPU game with the sound logged and the mix recorded (nothing reaches the speakers).
#   run_test.sh <name> <seconds> [extra game flags]   -> D:/TAGAudio/work/<name>.log, <name>.wav
N=$1; S=$2; shift 2
P="/c/Users/soon2/Documents/Madden TAG/Unreal/TAGGridiron"
"/c/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe" "$(cygpath -w "$P/TAGGridiron.uproject")" -game -RenderOffscreen -ForceRes -ResX=1280 -ResY=720 \
  -LiveCoding=0 -AsyncLoadingThread -NoLoadingScreen -TagNoLoad -TagSoak=$((S+30)) -TagAudioLog -TagAudioSilent -TagAudioRec=$S -unattended -nosplash \
  "-ini:Engine:[Audio]:UnfocusedVolumeMultiplier=1.0" "-abslog=D:/TAGAudio/work/$N.log" "$@" > /dev/null 2>&1
cp "$P/Saved/AudioRec/tag_audio.wav" "/d/TAGAudio/work/$N.wav"
grep "TAG audio" "/d/TAGAudio/work/$N.log" | sed 's/^\[[^]]*\]\[[ 0-9]*\]LogTemp: Display: //' > "/d/TAGAudio/work/$N.txt"
wc -l < "/d/TAGAudio/work/$N.txt"
