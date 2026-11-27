# Unreal Editor Python (headless). Retargets the spectator clips (UEFN skeleton) onto the MetaHuman body skeleton the
# MetaHuman Crowd Sample's merged crowd meshes use, for the stadium hero crowd.
#   UnrealEditor-Cmd.exe TAGGridiron.uproject -run=pythonscript -script="<this file>" -unattended -nosplash -nullrhi
# In:  /Game/TAG/Anim/Authored/A_TAG_Fan_Cheer_Loop and A_TAG_Idle_Stand_Loop (mocap pack retargets)
# Out: /Game/TAG/Crowd/Anims/A_Crowd_Cheer, A_Crowd_Idle  (report: Saved/crowd_retarget.json)
import json
import os

import unreal

EAL = unreal.EditorAssetLibrary
OUT_DIR = "/Game/TAG/Crowd/Anims"
REPORT = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "crowd_retarget.json")
SRC_MESH = "/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin"
TGT_MESH = "/MetaHumanCrowdContent/Characters/Assembled/AverageMale_Head01/MHC_AverageMale_Head01_CombinedSkelMesh"
RTG = "/Game/TAG/Anim/RTG_TAG_UEFN_to_MH"
CLIPS = {"A_Crowd_Cheer": "/Game/TAG/Anim/Authored/A_TAG_Fan_Cheer_Loop",
         "A_Crowd_Idle": "/Game/TAG/Anim/Authored/A_TAG_Idle_Stand_Loop"}
# the Mass crowd's high-res actors animate SKM_MetaHumanCrowd (SKEL_MetaHumanCrowd) through ABP_MassCrowdActor
ACTOR_MESH = "/MetaHumanCrowdContent/Animations/SKM_MetaHumanCrowd"
ACTOR_CLIPS = {"A_CrowdActor_Cheer": "/Game/TAG/Anim/Authored/A_TAG_Fan_Cheer_Loop"}

rep = {"out": {}, "errors": []}
try:
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(["/MetaHumanCrowdContent"], True)
    src_mesh = unreal.load_asset(SRC_MESH)
    tgt_mesh = unreal.load_asset(TGT_MESH)
    rtg = unreal.load_asset(RTG)
    if not (src_mesh and tgt_mesh and rtg):
        raise RuntimeError("missing source mesh / target mesh / retargeter: %s %s %s" % (src_mesh, tgt_mesh, rtg))
    if not EAL.does_directory_exist(OUT_DIR):
        EAL.make_directory(OUT_DIR)
    jobs = [(n, p, tgt_mesh) for n, p in CLIPS.items()] + [(n, p, unreal.load_asset(ACTOR_MESH)) for n, p in ACTOR_CLIPS.items()]
    for out_name, src_path, target in jobs:
        dst = OUT_DIR + "/" + out_name
        if EAL.does_asset_exist(dst):
            EAL.delete_asset(dst)
        data = unreal.EditorAssetLibrary.find_asset_data(src_path)
        results = unreal.IKRetargetBatchOperation.duplicate_and_retarget(
            assets_to_retarget=[data], source_mesh=src_mesh, target_mesh=target, ik_retarget_asset=rtg,
            prefix="RTG_", target_path=OUT_DIR, use_source_path=False, include_referenced_assets=False,
            overwrite_existing_files=True)
        made = [str(r.package_name) for r in results]
        anim_path = next((m for m in made if isinstance(unreal.load_asset(m), unreal.AnimSequence)), None)
        if not anim_path:
            raise RuntimeError("retarget produced no AnimSequence for %s: %s" % (out_name, made))
        if anim_path != dst:
            EAL.rename_asset(anim_path, dst)
        a = unreal.load_asset(dst)
        a.set_editor_property("enable_root_motion", False)
        EAL.save_asset(dst, only_if_is_dirty=False)
        sk = a.get_editor_property("skeleton")
        rep["out"][out_name] = {"asset": dst, "skeleton": sk.get_path_name() if sk else None,
                                "length_s": round(a.get_play_length(), 3)}
except Exception:
    import traceback
    rep["errors"].append(traceback.format_exc()[-2000:])
json.dump(rep, open(REPORT, "w"), indent=1)
unreal.log("TAG_CROWD_RETARGET " + ("ok" if not rep["errors"] else "FAILED") + " " + json.dumps(rep)[:1500])
