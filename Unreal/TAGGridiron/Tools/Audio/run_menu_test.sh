#!/bin/bash
# The scripted walk through the menus (-TagMenuAudio: UI/MenuAudioTest.cpp), silent and offscreen, with the sound logged
# and the mix recorded. Nothing reaches the speakers and nothing is saved.
#   run_menu_test.sh <name> [project dir]   -> D:/TAGAudio/work/<name>.log, <name>.txt (the sounds, in order), <name>.wav
N=$1
P=${2:-"/c/Users/soon2/Documents/Madden TAG/Unreal/TAGGridiron"}
"/c/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe" "$(cygpath -w "$P/TAGGridiron.uproject")" -game -RenderOffscreen -ForceRes -ResX=1280 -ResY=720 \
  -LiveCoding=0 -AsyncLoadingThread -NoLoadingScreen -TagNoLoad -TagMenuAudio -TagAudioLog -TagAudioSilent -TagAudioRec=150 -unattended -nosplash \
  "-ini:Engine:[Audio]:UnfocusedVolumeMultiplier=1.0" "-abslog=D:/TAGAudio/work/$N.log" > /dev/null 2>&1
cp "$P/Saved/AudioRec/tag_audio.wav" "/d/TAGAudio/work/$N.wav"
grep -E "TAG audio|TAG menuaudio" "/d/TAGAudio/work/$N.log" | sed 's/^\[[^]]*\]\[[ 0-9]*\]LogTemp: Display: //' > "/d/TAGAudio/work/$N.txt"
wc -l < "/d/TAGAudio/work/$N.txt"
