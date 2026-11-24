# Unreal Editor Python (headless). The stand idles of the pack bodies' motion matching split three ways
# (/Game/TAG/AnimH/MM/PSD_TAG_Stand_Idles_S1..S3, copies of PSD_TAG_Stand_Idles each holding a third of its captures;
# AAthlete takes S(id % 3 + 1) whenever he stands). With one idle database for all, every man standing still searched the
# same eleven idles and one won: A_TAG_Loco_Idle_Neutral2 on four or more men in a fifth of the post-play and pre-snap
# frames (the "4+ men on one loop" share, 0.215 in p81).
#   UnrealEditor-Cmd.exe TAGGridiron.uproject -run=pythonscript -script="<this file>" -unattended -nosplash -nullrhi
# Report: Saved/mm_idle_sets_hero.json
import json
import os

import unreal

EAL = unreal.EditorAssetLibrary
LIB = unreal.TagEditorLib
MM = "/Game/TAG/AnimH/MM/"
SRC = MM + "PSD_TAG_Stand_Idles"
SAVED = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir())
REPORT = os.path.join(SAVED, "mm_idle_sets_hero.json")
rep = {"sets": {}, "errors": []}
try:
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    reg.scan_paths_synchronous(["/Game/TAG/AnimH"], True)
    by_name = {}
    for d in reg.get_assets(unreal.ARFilter(package_paths=["/Game/TAG/AnimH"], recursive_paths=True)):
        by_name.setdefault(str(d.asset_name), str(d.package_name))
    src = unreal.load_asset(SRC)
    names = sorted(str(n) for n in LIB.database_anim_names(src))
    rep["source"] = names
    for v in (1, 2, 3):
        path = SRC + "_S%d" % v
        if EAL.does_asset_exist(path):
            EAL.delete_asset(path)
        EAL.duplicate_asset(SRC, path)
        db = unreal.load_asset(path)
        want = [n for i, n in enumerate(names) if i % 3 == v - 1]
        # (cleared, then the third added by asset: removal goes by substring, and Idle_Neutral is part of Idle_Neutral2)
        LIB.database_remove_anims_containing(db, "_")
        added, missing = [], []
        for n in want:
            a = unreal.load_asset(by_name[n]) if n in by_name else None
            if a and LIB.database_add_anim(db, a):
                added.append(n)
            else:
                missing.append(n)
        EAL.save_asset(path, only_if_is_dirty=False)
        rep["sets"][path] = {"want": want, "added": added, "missing": missing, "anims": [str(n) for n in LIB.database_anim_names(db)]}
except Exception:
    import traceback
    rep["errors"].append(traceback.format_exc())
json.dump(rep, open(REPORT, "w"), indent=1)
unreal.log("TAG_IDLE_SETS %s" % json.dumps({k: len(v["anims"]) for k, v in rep["sets"].items()}))
