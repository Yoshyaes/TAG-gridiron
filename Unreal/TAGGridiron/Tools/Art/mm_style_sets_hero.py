# Unreal Editor Python (headless). Locomotion by body class for the pack bodies' motion matching (/Game/TAG/AnimH/MM).
# Each man's style set (PSD_TAG_<Gait>_Loops_S1..S6, AAthlete: player id % 6) mixed every body the pack filmed: a 330 lb
# tackle ran on a corner's capture and a corner on a big man's (Research/NextLevel/01-animation-motion.md, action 1).
# For every gait and style set this writes, per class (Big: the line; Med: linebackers, tight ends, fullbacks, power
# backs; Agile: receivers, the secondary, light backs - AAthlete::BodyClass):
#   PSD_TAG_<Gait>_Loops_S<n>_<Class>      the set's captures of that class plus its general ones (no body in the name)
#   PSD_TAG_<Gait>_Loops_S<n>_<Class>_Off  the set's captures of the other classes, BaseCostBias +0.1 (TAG_STYLE_BIAS):
#                                          they win only where nothing of his own fits - a class has 8-12 captures in a
#                                          set, too few to cover every direction and speed alone
# and per gait the tired captures (MG_NonBC_Fatigued_MC_*), out of every class set, on their own:
#   PSD_TAG_<Gait>_Loops_Fatigued          searched besides only by a tired man after a live play's first 2 s
# Every set is a duplicate of the style set it splits (same schema, settings and sampling ranges), its other entries
# removed; In + Off is the set without its tired captures. Costs only compare across databases normalized together: a
# set that has a normalization set keeps it (the duplicate carries it); one that has none gets PSN_TAG_<Gait>_S<n>_Style
# over its six splits, whose data together are the set's own, so the costs sit on the set's old scale.
# The class rules are Tools/Anim/mm_classes.py, the same as FAthleteAssets::CaptureClass (Athlete.cpp): keep the two the
# same. The old style sets are not touched (-TagNoStyleSets plays them).
#   UnrealEditor-Cmd.exe TAGGridiron.uproject -run=pythonscript -script="<this file>" -unattended -nosplash -nullrhi
#   (TAG_STYLE_DRY=1: the census and the plan only, nothing written)
# Report: Saved/mm_style_sets_hero.json (what each source set holds by class, what each new set holds, rule checks)
import json
import os
import sys

import unreal

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Anim"))
import mocap_map  # noqa: E402
from mm_classes import CLASSES, capture_class, pack_class  # noqa: E402  (the game's rules: FAthleteAssets::CaptureClass)

EAL = unreal.EditorAssetLibrary
LIB = unreal.TagEditorLib
MM = "/Game/TAG/AnimH/MM/"
SAVED = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir())
REPORT = os.path.join(SAVED, "mm_style_sets_hero.json")
BIAS = float(os.environ.get("TAG_STYLE_BIAS", "0.1"))
DRY = os.environ.get("TAG_STYLE_DRY", "") == "1"
GAITS = ("Walk", "Run", "Sprint")
rep = {"bias": BIAS, "dry": DRY, "source": {}, "built": {}, "fatigued": {}, "normalization": {}, "rule_check": [], "errors": []}


def anim_names(db):
    return [str(n) for n in LIB.database_anim_names(db)]


def census(names):
    c = {}
    for n in names:
        k = capture_class(n)
        c[k] = c.get(k, 0) + 1
    return c


def set_bias(db, bias):
    try:
        db.set_editor_property("base_cost_bias", bias)
    except Exception as e:  # noqa: BLE001
        rep["errors"].append("base_cost_bias on %s: %s" % (db.get_name(), e))


def build_db(dst, src_path, keep, bias, by_name):
    """dst as src_path with only `keep` left (duplicated when new, so sampling ranges and settings are the source's)."""
    fresh = not EAL.does_asset_exist(dst)
    db = EAL.duplicate_asset(src_path, dst) if fresh else unreal.load_asset(dst)
    if not db:
        rep["errors"].append("no database " + dst)
        return None
    keep_set = set(keep)
    removed = 0
    for n in anim_names(db):
        if n not in keep_set:
            removed += LIB.database_remove_anims_containing(db, n)
    # (the remove takes every name containing the one given: A_TAG_Loco_Run_Side took A_TAG_Loco_Run_Side_M with it)
    have = set(anim_names(db))
    added, missing = [], []
    for n in keep:
        if n in have:
            continue
        path = by_name.get(n)
        a = unreal.load_asset(path) if path else None
        if a and LIB.database_add_anim(db, a):
            added.append(n)
            if n.startswith("A_TAG_Pack_"):
                LIB.database_trim_ends(db, n, 0.5)  # (as mm_build_dbs_hero.py indexes the pack's loops)
        else:
            missing.append(n)
    set_bias(db, bias)
    now = anim_names(db)
    if sorted(now) != sorted(keep):
        rep["errors"].append("%s holds %d entries, %d wanted (extra %s, missing %s)" % (
            dst, len(now), len(keep), sorted(set(now) - keep_set)[:4], sorted(keep_set - set(now))[:4]))
    EAL.save_asset(dst, only_if_is_dirty=False)
    rep["built"][dst.rsplit("/", 1)[1]] = {"anims": now, "classes": census(now), "bias": bias, "fresh": fresh,
                                           "removed": removed, "added_back": added, "missing": missing}
    return db


def make_normalization_set(path, dbs):
    try:
        if EAL.does_asset_exist(path):
            ns = unreal.load_asset(path)
        else:
            f = unreal.DataAssetFactory()
            f.set_editor_property("data_asset_class", unreal.PoseSearchNormalizationSet)
            folder, name = path.rsplit("/", 1)
            ns = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.PoseSearchNormalizationSet, f)
        ns.set_editor_property("databases", dbs)
        EAL.save_asset(path, only_if_is_dirty=False)
        return ns
    except Exception as e:  # noqa: BLE001
        rep["errors"].append("normalization set %s: %s" % (path, e))
        return None


try:
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    reg.scan_paths_synchronous(["/Game/TAG/AnimH"], True)
    by_name = {str(d.asset_name): str(d.package_name) for d in reg.get_assets(unreal.ARFilter(
        package_paths=["/Game/TAG/AnimH/Authored", "/Game/TAG/AnimH/Pack"], recursive_paths=True))}
    # the authored loops' rule against the captures they were cut from
    for k, v in mocap_map.LOCO.items():
        n, src = "A_TAG_" + k, v.get("src", "")
        if capture_class(n) != pack_class(src):
            rep["rule_check"].append({"name": n, "src": src, "by_name": capture_class(n), "by_src": pack_class(src)})
    for gait in GAITS:
        # the tired captures of the gait, from its whole loop list
        full = MM + "PSD_TAG_%s_Loops" % gait
        if EAL.does_asset_exist(full):
            fdb = unreal.load_asset(full)
            tired = [n for n in anim_names(fdb) if capture_class(n) == "Fatigued"]
            rep["fatigued"][gait] = tired
            if tired and not DRY:
                build_db(MM + "PSD_TAG_%s_Loops_Fatigued" % gait, full, tired, 0.0, by_name)
        for v in range(1, 7):
            src_path = MM + "PSD_TAG_%s_Loops_S%d" % (gait, v)
            if not EAL.does_asset_exist(src_path):
                continue
            src = unreal.load_asset(src_path)
            names = anim_names(src)
            by_cls = {}
            for n in names:
                by_cls.setdefault(capture_class(n), []).append(n)
            rep["source"]["PSD_TAG_%s_Loops_S%d" % (gait, v)] = {"n": len(names), "classes": census(names), "anims": {k: sorted(x) for k, x in by_cls.items()}}
            if DRY:
                continue
            made = []
            for c in CLASSES:
                own = [n for n in names if capture_class(n) in (c, "General")]
                other = [n for n in names if capture_class(n) in CLASSES and capture_class(n) != c]
                base = MM + "PSD_TAG_%s_Loops_S%d_%s" % (gait, v, c)
                if own:
                    db = build_db(base, src_path, own, 0.0, by_name)
                    if db:
                        made.append(db)
                if other:
                    db = build_db(base + "_Off", src_path, other, BIAS, by_name)
                    if db:
                        made.append(db)
                elif EAL.does_asset_exist(base + "_Off"):
                    rep["errors"].append(base + "_Off exists but has nothing to hold now: left empty (the game skips an empty set)")
                    db = unreal.load_asset(base + "_Off")
                    LIB.database_remove_anims_containing(db, "_")
                    EAL.save_asset(base + "_Off", only_if_is_dirty=False)
            # normalization: the source's if it has one (the duplicates carry it), else one over the six splits
            ns_src = None
            try:
                ns_src = src.get_editor_property("normalization_set")
            except Exception:  # noqa: BLE001
                pass
            key = "PSD_TAG_%s_Loops_S%d" % (gait, v)
            if ns_src:
                rep["normalization"][key] = "kept " + ns_src.get_path_name()
            elif made:
                ns = make_normalization_set(MM + "PSN_TAG_%s_S%d_Style" % (gait, v), made)
                if ns:
                    for db in made:
                        db.set_editor_property("normalization_set", ns)
                        EAL.save_asset(db.get_path_name().split(".")[0], only_if_is_dirty=False)
                    rep["normalization"][key] = "new " + ns.get_path_name()
except Exception:  # noqa: BLE001
    import traceback
    rep["errors"].append(traceback.format_exc()[-2000:])
json.dump(rep, open(REPORT, "w"), indent=1)
unreal.log("TAG_STYLE_SETS " + json.dumps({"source": {k: v["classes"] for k, v in rep["source"].items()},
                                           "built": len(rep["built"]), "fatigued": {k: len(v) for k, v in rep["fatigued"].items()},
                                           "rule_check": len(rep["rule_check"]), "errors": rep["errors"][:5]})[:3000])
