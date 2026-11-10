#!/usr/bin/env bash
# Retarget the whole mocap pack onto the UEFN rig as a library, in parallel Blender processes:
#   Tools/Anim/build_pack_library.sh [-o out_dir] [-j shards] [names.txt]
# Default: every clip of the manifest (D:\MaddenRef\MocapPack\manifest.csv), 8 shards, out RawArt/Anim/Pack
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
    -o) OUT="$2"; shift 2;;
    -j) JOBS="$2"; shift 2;;
    *) break;;
  esac
done
LOGS="$PROJ/Saved/PackBuild"
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
