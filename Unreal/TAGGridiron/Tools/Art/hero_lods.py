# Unreal Editor Python (headless). Reduced LODs for the heavier model pack characters that stand in the stands and on
# the sideline (story characters, fans, coaches in their own clothes), so a full front row costs little at broadcast
# distance. Env TAG_HERO_LOD_DIRS (comma list of folders under /Game/TAG/Hero, default Story,Crowd,Coaches),
# TAG_HERO_LOD_COUNT (default 3). Report: Saved/hero_lods.json
import json
import os

import unreal

EAL = unreal.EditorAssetLibrary
SAVED = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir())
dirs = os.environ.get("TAG_HERO_LOD_DIRS", "Story,Crowd,Coaches").split(",")
count = int(os.environ.get("TAG_HERO_LOD_COUNT", "3"))
sub = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
rep = {"done": 0, "failed": [], "skipped": 0}
for d in dirs:
    for p in EAL.list_assets("/Game/TAG/Hero/" + d, recursive=False, include_folder=False):
        p = p.split(".")[0]
        m = unreal.load_asset(p)
        if not isinstance(m, unreal.SkeletalMesh):
            continue
        try:
            if sub.get_lod_count(m) >= count:
                rep["skipped"] += 1
                continue
            ok = sub.regenerate_lod(m, count, True, False)
            if not ok:
                rep["failed"].append(p)
                continue
            EAL.save_asset(p, only_if_is_dirty=False)
            rep["done"] += 1
        except Exception as e:
            rep["failed"].append("%s %r" % (p, e))
json.dump(rep, open(os.path.join(SAVED, "hero_lods.json"), "w"), indent=1)
unreal.log("TAG_HERO_LODS %s" % json.dumps({k: (v if not isinstance(v, list) else len(v)) for k, v in rep.items()}))
