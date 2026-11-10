# Unreal Editor Python (headless, read only): every imported locomotion clip (/Game/TAG/Anim/Authored/A_TAG_Loco_*)
# with its length, mean root speed and travel direction (deg from the clip's facing, + to the right), from the root
# bone's world pose at the start and end. What the motion matching databases actually cover.
#   UnrealEditor-Cmd.exe TAGGridiron.uproject -run=pythonscript -script="<this file>" -unattended -nosplash -nullrhi
import math

import unreal

EAL = unreal.EditorAssetLibrary
opts = unreal.AnimPoseEvaluationOptions()
for path in sorted(EAL.list_assets("/Game/TAG/Anim/Authored", recursive=False)):
    name = path.split("/")[-1].split(".")[0]
    if not name.startswith("A_TAG_Loco_"):
        continue
    anim = unreal.load_asset(path.split(".")[0])
    L = anim.get_play_length()
    p0 = unreal.AnimPoseExtensions.get_bone_pose(unreal.AnimPoseExtensions.get_anim_pose_at_time(anim, 0.0, opts), "root", unreal.AnimPoseSpaces.WORLD).translation
    p1 = unreal.AnimPoseExtensions.get_bone_pose(unreal.AnimPoseExtensions.get_anim_pose_at_time(anim, L, opts), "root", unreal.AnimPoseSpaces.WORLD).translation
    dx, dy = p1.x - p0.x, p1.y - p0.y
    dist = math.hypot(dx, dy)
    unreal.log("TAG_LOCO %-36s len %.2f s  %4.1f m/s  dir %5.0f  travel %.1f m" % (name[6:], L, dist / 100.0 / max(L, 0.01), math.degrees(math.atan2(dy, dx)) if dist > 5 else 0.0, dist / 100.0))
