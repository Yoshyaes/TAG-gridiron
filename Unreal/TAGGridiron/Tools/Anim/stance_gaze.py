# Unreal Editor Python (headless). Head pitch of the linemen's 3-point stance clip as authored, on each animation tree
# (the hero-skeleton tree /Game/TAG/AnimH = the capture; the UEFN tree it was retargeted through): the head's bind
# forward axis below (-) or above the horizon, degrees, mean / min / max over the loop. Compare with the game's
# "TAG presnap gaze" (-TagLineTest -TagNoPresnapLook shows the stance as played, without the pre-snap look).
import unreal
EAL = unreal.EditorAssetLibrary
APE = unreal.AnimPoseExtensions
opts = unreal.AnimPoseEvaluationOptions()
for p in EAL.list_assets("/Game/TAG", recursive=True, include_folder=False):
    if not p.split(".")[0].endswith("/A_TAG_Stance3pt_Loop"):
        continue
    a = unreal.load_asset(p.split(".")[0])
    if not isinstance(a, unreal.AnimSequence):
        continue
    ref = APE.get_reference_pose(a.get_editor_property("skeleton"))
    head = "head"
    rb = APE.get_bone_pose(ref, head, unreal.AnimPoseSpaces.WORLD).rotation
    fwd_local = rb.unrotate_vector(unreal.Vector(0, 1, 0))
    L = a.get_play_length(); n = max(2, int(L * 30)); ps = []
    import math
    for i in range(n + 1):
        pose = APE.get_anim_pose_at_time(a, L * i / n, opts)
        r = APE.get_bone_pose(pose, head, unreal.AnimPoseSpaces.WORLD).rotation
        f = r.rotate_vector(fwd_local)
        ps.append(math.degrees(math.atan2(f.z, math.hypot(f.x, f.y))))
    unreal.log("STANCEGAZE %-60s mean %.1f min %.1f max %.1f deg (%d samples)" % (p.split(".")[0], sum(ps) / len(ps), min(ps), max(ps), len(ps)))
