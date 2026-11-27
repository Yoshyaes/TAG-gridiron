#!/usr/bin/env bash
# Build TAG clips in Blender (4 at a time): Tools/Anim/build_all.sh [clip ...]   (no args: every clip in clips_more.MORE)
# Writes RawArt/Anim/Clips/A_TAG_<clip>.fbx + .json + preview renders. Then import with Tools/Art/import_tag_anims.py.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
PROJ="$(cd "$HERE/../.." && pwd)"
BLENDER="${BLENDER:-/c/Program Files/Blender Foundation/Blender 5.2/blender.exe}"
RIG="$PROJ/RawArt/Anim/UEFN_Rig_Idle.fbx"
OUT="$PROJ/RawArt/Anim/Clips"
LOGS="$PROJ/Saved/AnimBuild"
mkdir -p "$LOGS"
if [ $# -gt 0 ]; then NAMES="$*"; else
  NAMES=$(cd "$HERE" && python -c "import ast,sys;src=open('clips_more.py').read();t=ast.parse(src);
d=[n for n in t.body if isinstance(n,ast.Assign) and n.targets[0].id=='MORE'][0].value;print(' '.join(k.value for k in d.keys))")
fi
build() {
  "$BLENDER" --background --factory-startup --python "$HERE/build_clip.py" -- "$RIG" "$1" "$OUT" --render > "$LOGS/$1.log" 2>&1
  if grep -q "TAG_CLIP" "$LOGS/$1.log"; then echo "ok   $1"; else echo "FAIL $1"; fi
}
export -f build; export BLENDER RIG OUT LOGS HERE
printf '%s\n' $NAMES | xargs -P 4 -I{} bash -c 'build {}'
