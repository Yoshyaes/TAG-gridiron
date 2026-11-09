# Unreal Editor Python (headless). Builds /Game/TAG/Crowd/DA_TAG_FanConfig: the MetaHuman Crowd Sample's Mass entity
# config without its walking traits, so spectators keep the sample's appearance pipeline (MetaHuman Instances dressed
# per entity, LOD, high-res actors near the camera) but stay on their seats.
import json
import os

import unreal

EAL = unreal.EditorAssetLibrary
SRC = "/MetaHumanCrowdContent/AI/DA_MHMassCrowdConfig_Base"
DST = "/Game/TAG/Crowd/DA_TAG_FanConfig"
DROP = {"MassNavMeshNavigationTrait", "MassSteeringTrait", "CharacterTrajectoryMovementTrait", "MassSmartObjectUserTrait",
        "MassNavigationObstacleTrait", "SpringMovementTrait", "MassObstacleAvoidanceTrait", "MassStateTreeTrait",
        "MassSmoothOrientationTrait"}  # needs velocity / move target fragments from the movement traits
REPORT = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "crowd_fan_config.json")
rep = {"kept": [], "dropped": [], "errors": []}
try:
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(["/MetaHumanCrowdContent"], True)
    if EAL.does_asset_exist(DST):
        EAL.delete_asset(DST)
    asset = EAL.duplicate_asset(SRC, DST)
    cfg = asset.get_editor_property("config")
    kept = []
    for t in cfg.get_editor_property("traits"):
        n = t.get_class().get_name()
        (rep["dropped"] if n in DROP else rep["kept"]).append(n)
        if n not in DROP:
            kept.append(t)
    cfg.set_editor_property("traits", kept)
    asset.set_editor_property("config", cfg)
    EAL.save_asset(DST, only_if_is_dirty=False)
except Exception:
    import traceback
    rep["errors"].append(traceback.format_exc()[-1500:])
json.dump(rep, open(REPORT, "w"), indent=1)
unreal.log("TAG_FAN_CONFIG " + json.dumps(rep)[:1500])
