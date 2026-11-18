# Unreal Editor Python (headless). The walk style sets of the pack bodies' motion matching (/Game/TAG/AnimH/MM/
# PSD_TAG_Walk_Loops_S1..S3) rebuilt straight on the hero skeleton: the pack's walk loops (Saved/pack_loco_lists.json
# "Walk", which now holds the 14 constant-pace forward walk loops of PostPlay / Blocking) split three ways by direction
# like mm_build_dbs.style_subset, six ways (S1-S6, AAthlete: player id % 6) where a direction has six captures or more.
# Without the constant-pace walks a man walking back to the huddle had one forward walk per set and four or more men
# walked in step on one capture (the "4+ men on one loop" share, 0.19 in bc7).
# (mm_build_dbs.py on the UEFN rig crashed in its PoseSearch index build - compressed data ensure - so these sets are
# written here; the sample's and the hand-listed clips already in each set are kept.)
#   UnrealEditor-Cmd.exe TAGGridiron.uproject -run=pythonscript -script="<this file>" -unattended -nosplash -nullrhi
# Report: Saved/mm_walk_sets_hero.json
import json
import os
import re

import unreal

EAL = unreal.EditorAssetLibrary
LIB = unreal.TagEditorLib
MM = "/Game/TAG/AnimH/MM/"
SAVED = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir())
REPORT = os.path.join(SAVED, "mm_walk_sets_hero.json")
rep = {"sets": {}, "errors": []}


def style_subset(names, v):
    groups = {}
    for n in names:
        b = n.rsplit("/", 1)[-1]
        m = re.search(r"_D(\d)", b)
        groups.setdefault(m.group(1) if m else "1" if "_FWD_" in b else "?", []).append(n)
    keep = []
    for k, g in groups.items():
        g = sorted(g)
        v3 = (v - 1) % 3 + 1  # (S4-S6 split a direction six ways when it has six captures, else repeat S1-S3)
        if len(g) >= 6:
            keep += [n for i, n in enumerate(g) if i % 6 == v - 1]
        elif len(g) >= 3:
            keep += [n for i, n in enumerate(g) if i % 3 == v3 - 1]
        elif len(g) == 2:
            keep += g if v3 == 3 else [g[v3 - 1]]
        else:
            keep += g
    return keep


try:
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    reg.scan_paths_synchronous(["/Game/TAG/AnimH"], True)
    by_name = {str(d.asset_name): str(d.package_name) for d in reg.get_assets(unreal.ARFilter(
        package_paths=["/Game/TAG/AnimH/Pack"], recursive_paths=True))}
    walk = [p.rsplit("/", 1)[1] for p in json.load(open(os.path.join(SAVED, "pack_loco_lists.json")))["Walk"]]
    for v in (1, 2, 3, 4, 5, 6):
        path = MM + "PSD_TAG_Walk_Loops_S%d" % v
        # (S4-S6 start as copies of S1-S3: the sample's and hand-listed clips of that third)
        if not EAL.does_asset_exist(path):
            EAL.duplicate_asset(MM + "PSD_TAG_Walk_Loops_S%d" % (v - 3), path)
        db = unreal.load_asset(path)
        if not db:
            rep["errors"].append("no " + path)
            continue
        want = set(style_subset(walk, v))
        have = [str(n) for n in LIB.database_anim_names(db)]
        removed = [n for n in have if n.startswith("A_TAG_Pack_") and n in walk and n not in want]
        for n in removed:
            LIB.database_remove_anims_containing(db, n)
        added, missing = [], []
        for n in sorted(want):
            if n in have:
                continue
            a = unreal.load_asset(by_name[n]) if n in by_name else None
            if a and LIB.database_add_anim(db, a):
                added.append(n)
            else:
                missing.append(n)
        trimmed = LIB.database_trim_ends(db, "A_TAG_Pack_", 0.5)
        EAL.save_asset(path, only_if_is_dirty=False)
        rep["sets"][path] = {"added": added, "removed": removed, "missing": missing, "trimmed": trimmed,
                             "anims": [str(n) for n in LIB.database_anim_names(db)]}
except Exception:
    import traceback
    rep["errors"].append(traceback.format_exc()[-1500:])
json.dump(rep, open(REPORT, "w"), indent=1)
