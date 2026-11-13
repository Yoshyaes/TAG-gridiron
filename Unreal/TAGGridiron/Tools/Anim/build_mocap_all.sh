#!/usr/bin/env bash
# Build TAG clips from the mocap pack in Blender (4 at a time):
#   Tools/Anim/build_mocap_all.sh [-o out_dir] [clip ...]     (no clips: every key of mocap_map.MAP)
# Writes <out_dir>/A_TAG_<clip>.fbx + .json + preview renders (default out_dir RawArt/Anim/Clips, the folder
# Tools/Art/import_tag_anims.py imports). Logs in Saved/MocapBuild. Then import with import_tag_anims.py.
#   Tools/Anim/build_mocap_all.sh -r hero [clip ...]   the same clips on the hero rig (TAG_ANIM_RIG=hero to import)
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
PROJ="$(cd "$HERE/../.." && pwd)"
BLENDER="${BLENDER:-/c/Program Files/Blender Foundation/Blender 5.2/blender.exe}"
RIG="$PROJ/RawArt/Anim/UEFN_Rig_Idle.fbx"
OUT="$PROJ/RawArt/Anim/Clips"
LOGS="$PROJ/Saved/MocapBuild"
# -r hero: onto the pack's own skeleton (the model-pack bodies' hero rig), out RawArt/Anim/ClipsHero
if [ "${1:-}" = "-r" ]; then if [ "$2" = hero ]; then RIG="$PROJ/RawArt/Hero/Rig/SKM_TAG_HeroRig.fbx"; OUT="$PROJ/RawArt/Anim/ClipsHero"; LOGS="$PROJ/Saved/MocapBuildHero"; fi; shift 2; fi
if [ "${1:-}" = "-o" ]; then OUT="$2"; shift 2; fi
JOBS="${JOBS:-4}"
mkdir -p "$LOGS" "$OUT"
if [ $# -gt 0 ]; then NAMES="$*"; else
  NAMES=$(cd "$HERE" && python -c "import mocap_map;print(' '.join(mocap_map.all_names()))")
fi
build() {
  "$BLENDER" --background --factory-startup --python "$HERE/build_mocap.py" -- "$RIG" "$1" "$OUT" --render > "$LOGS/$1.log" 2>&1
  if grep -q "TAG_CLIP" "$LOGS/$1.log"; then echo "ok   $1"; else echo "FAIL $1: $(grep -m1 -E 'Error|error|KeyError|Exception' "$LOGS/$1.log" | cut -c1-160)"; fi
}
export -f build; export BLENDER RIG OUT LOGS HERE
printf '%s\n' $NAMES | xargs -P "$JOBS" -I{} bash -c 'build {}'
