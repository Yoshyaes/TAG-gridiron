# Unreal Editor Python (headless, long running). Rebuilds the MetaHuman Crowd Sample's crowd for TAG so distant
# instanced fans can cheer: the sample bakes only idle (0) and walk (1) into the transform tracks distant fans play.
#   1. /Game/TAG/Crowd/AnimConfig_TAG  = the sample's AnimConfig + entry 2 "Cheer" (our keyed cheer on SKEL_MetaHumanCrowd)
#   2. /Game/TAG/Crowd/CrowdCollection_TAG = the sample's collection, its crowd pipeline pointed at AnimConfig_TAG, built
#   3. /Game/TAG/Crowd/Instances/*      = the sample's 255 instances re-pointed at the TAG collection
#   4. DA_TAG_FanConfig's visualization trait uses the TAG instances
# Epic's plugin content is never modified (it is a junction into the engine install). Runtime: ATagFanSpawner sets
# sequence 2 on hyped fans (UTagFanCheerProcessor).
import json
import os
import time

import unreal

EAL = unreal.EditorAssetLibrary
LIB = unreal.TagEditorLib
SRC = "/MetaHumanCrowdContent/"
DST = "/Game/TAG/Crowd/"
CHEER = "/Game/TAG/Crowd/Anims/A_CrowdActor_Cheer"
REPORT = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "crowd_rebuild.json")
rep = {"steps": [], "errors": []}


def copy_or_load(src, dst):
    """The TAG copy of an asset: reused when it already exists (a previous, interrupted run), else duplicated."""
    if EAL.does_asset_exist(dst):
        o = unreal.load_asset(dst)
        if o:
            return o
    o = EAL.duplicate_asset(src, dst)
    if not o:
        raise RuntimeError("could not create " + dst)
    return o


def step(msg):
    rep["steps"].append(msg)
    unreal.log("TAG_CROWD_REBUILD " + msg)
    json.dump(rep, open(REPORT, "w"), indent=1)


try:
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(["/MetaHumanCrowdContent", "/Game/TAG/Crowd"], True)
    # 1. animation config with the cheer as entry 2
    cfg = copy_or_load(SRC + "Animations/AnimConfig", DST + "AnimConfig_TAG")
    entries = list(cfg.get_editor_property("animations_to_bake"))
    step("source entries: " + ", ".join(str(e.get_editor_property("name")) for e in entries))
    cheer = unreal.load_asset(CHEER)
    if not cheer:
        raise RuntimeError("missing " + CHEER + " (run Tools/Art/crowd_retarget.py)")
    e = unreal.MetaHumanCrowdBakeAnimationData()
    e.set_editor_property("name", "Cheer")
    e.set_editor_property("use_merged_animation", False)
    e.set_editor_property("body_anim_sequence", cheer)
    e.set_editor_property("face_anim_sequence", entries[0].get_editor_property("face_anim_sequence"))  # idle face
    e.set_editor_property("loop", True)
    e.set_editor_property("root_motion", False)
    entries = [x for x in entries if str(x.get_editor_property("name")) != "Cheer"][:2] + [e]
    cfg.set_editor_property("animations_to_bake", entries)
    EAL.save_asset(DST + "AnimConfig_TAG", only_if_is_dirty=False)
    step("AnimConfig_TAG entries: " + ", ".join(str(x.get_editor_property("name")) for x in entries))

    # 2. collection copy, pointed at the TAG config, built
    coll = copy_or_load(SRC + "CrowdCollection", DST + "CrowdCollection_TAG")
    if not LIB.crowd_set_animation_config(coll, cfg):
        raise RuntimeError("could not set the crowd pipeline's AnimationConfig")
    step("collection duplicated and configured; building (this takes a while)")
    t0 = time.time()
    ok = LIB.crowd_build_collection(coll, 3 * 3600.0)
    step("build %s in %.0f s" % ("succeeded" if ok else "FAILED", time.time() - t0))
    if not ok:
        raise RuntimeError("collection build failed")
    EAL.save_asset(DST + "CrowdCollection_TAG", only_if_is_dirty=False)

    # 3. instances re-pointed at the TAG collection
    fan = unreal.load_asset(DST + "DA_TAG_FanConfig")
    cfgs = fan.get_editor_property("config")
    traits = list(cfgs.get_editor_property("traits"))
    vis = [t for t in traits if "Visualization" in t.get_class().get_name()][0]
    new_instances = []
    for inst in vis.get_editor_property("character_instances"):
        if not inst:
            continue
        name = inst.get_name()
        dst = DST + "Instances/" + name
        ni = copy_or_load(inst.get_path_name().split(".")[0], dst)
        LIB.crowd_set_instance_collection(ni, coll)
        EAL.save_asset(dst, only_if_is_dirty=False)
        new_instances.append(ni)
    step("instances re-pointed: %d" % len(new_instances))

    # 4. fan config uses them
    vis.set_editor_property("character_instances", new_instances)
    cfgs.set_editor_property("traits", traits)
    fan.set_editor_property("config", cfgs)
    EAL.save_asset(DST + "DA_TAG_FanConfig", only_if_is_dirty=False)
    step("DA_TAG_FanConfig now uses the TAG instances")
except Exception:
    import traceback
    rep["errors"].append(traceback.format_exc()[-2000:])
json.dump(rep, open(REPORT, "w"), indent=1)
unreal.log("TAG_CROWD_REBUILD done errors=%d" % len(rep["errors"]))
