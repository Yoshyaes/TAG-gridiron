#!/usr/bin/env bash
# Retarget the whole mocap pack onto the UEFN rig as a library, in parallel Blender processes:
#   Tools/Anim/build_pack_library.sh [-r uefn|hero] [-o out_dir] [-j shards] [names.txt]
# Default: every clip of the manifest (D:\MaddenRef\MocapPack\manifest.csv), 8 shards, out RawArt/Anim/Pack
# -r hero: onto the pack's own skeleton (the hero rig the model-pack bodies are skinned to): the capture itself, renamed,
# out RawArt/Anim/PackHero by default, imported by import_pack_library.py with TAG_ANIM_RIG=hero
# (<Category>/A_TAG_Pack_<name>.fbx + .json). Reruns resume (existing fbx skipped). Logs: Saved/PackBuild/shard_<i>.log.
# Then import with Tools/Art/import_pack_library.py.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
PROJ="$(cd "$HERE/../.." && pwd)"
BLENDER="${BLENDER:-/c/Program Files/Blender Foundation/Blender 5.2/blender.exe}"
RIG="$PROJ/RawArt/Anim/UEFN_Rig_Idle.fbx"
OUT="$PROJ/RawArt/Anim/Pack"
JOBS=8
while [ $# -gt 0 ]; do
  case "$1" in
    -r) if [ "$2" = hero ]; then RIG="$PROJ/RawArt/Hero/Rig/SKM_TAG_HeroRig.fbx"; OUT="${OUT_SET:-$PROJ/RawArt/Anim/PackHero}"; LOGSUB=PackBuildHero; fi; shift 2;;
    -o) OUT="$2"; OUT_SET="$2"; shift 2;;
    -j) JOBS="$2"; shift 2;;
    *) break;;
  esac
done
LOGS="$PROJ/Saved/${LOGSUB:-PackBuild}"
mkdir -p "$LOGS" "$OUT"
if [ $# -gt 0 ]; then NAMES="$1"; else
  NAMES="$LOGS/names.txt"
  tail -n +2 "${MOCAP_PACK:-/d/MaddenRef/MocapPack}/manifest.csv" | cut -d, -f1 > "$NAMES"
fi
echo "$(wc -l < "$NAMES") clips, $JOBS shards -> $OUT"
for ((i = 0; i < JOBS; i++)); do
  "$BLENDER" --background --factory-startup --python "$HERE/build_pack_library.py" -- "$RIG" "$OUT" "$NAMES" --shard "$i/$JOBS" > "$LOGS/shard_$i.log" 2>&1 &
done
wait
grep -h "TAG_PACK_DONE" "$LOGS"/shard_*.log
echo "ok $(grep -h '^TAG_PACK ok' "$LOGS"/shard_*.log | wc -l), failed $(grep -h '^TAG_PACK fail' "$LOGS"/shard_*.log | wc -l)"
