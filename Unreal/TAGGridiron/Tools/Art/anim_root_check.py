# Unreal Editor Python (headless, read only): root-motion check of imported TAG clips. Prints the root bone's world
# position at the start and end of each clip named in TAG_CHECK (comma separated A_TAG_ names), the root motion flag
# and the loop flag.
#   UnrealEditor-Cmd.exe TAGGridiron.uproject -run=pythonscript -script="<this file>" -unattended -nosplash -nullrhi
import os

import unreal

names = [n.strip() for n in os.environ.get("TAG_CHECK", "").split(",") if n.strip()]
for n in names:
    path = n if n.startswith("/Game/") else "/Game/TAG/Anim/Authored/" + n
    anim = unreal.load_asset(path)
    if not anim:
        unreal.log("TAG_ROOT_CHECK %s missing" % n)
        continue
    L = anim.get_play_length()
    opts = unreal.AnimPoseEvaluationOptions()
    out = []
    for t in (0.0, L * 0.5, L):
        pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(anim, t, opts)
        tr = unreal.AnimPoseExtensions.get_bone_pose(pose, "root", unreal.AnimPoseSpaces.WORLD)
        pel = unreal.AnimPoseExtensions.get_bone_pose(pose, "pelvis", unreal.AnimPoseSpaces.WORLD)
        out.append("t=%.2f root=(%.0f,%.0f,%.0f) yaw=%.0f pelvis=(%.0f,%.0f,%.0f)" % (t, tr.translation.x, tr.translation.y, tr.translation.z, tr.rotation.rotator().yaw, pel.translation.x, pel.translation.y, pel.translation.z))
    loop = None
    try:
        loop = anim.get_editor_property("loop")
    except Exception:
        pass
    extra = ""
    try:
        extra += " lock=%s" % anim.get_editor_property("root_motion_root_lock")
    except Exception:
        pass
    try:
        extra += " force_lock=%s" % anim.get_editor_property("force_root_lock")
    except Exception:
        pass
    try:
        tracks = list(unreal.AnimationLibrary.get_animation_track_names(anim))
        extra += " tracks=%d root_track=%s first=%s" % (len(tracks), "root" in [str(t) for t in tracks], [str(t) for t in tracks[:3]])
    except Exception as e:
        extra += " tracks? %s" % str(e)[:60]
    unreal.log("TAG_ROOT_CHECK %s len %.2f root_motion=%s loop=%s%s | %s" % (n.split("/")[-1], L, anim.get_editor_property("enable_root_motion"), loop, extra, " | ".join(out)))
