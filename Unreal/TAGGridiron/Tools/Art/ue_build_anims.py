# Unreal Editor Python (headless, -run=pythonscript): the TAG locomotion blend spaces from the mocap pack's clips
# (/Game/TAG/Anim/Authored/A_TAG_Loco_*, root motion, imported by import_tag_anims.py). They are the fallback when
# motion matching is unavailable (FAthleteAssets::Locomotion / Crouch); each sample sits at its clip's own direction
# and measured root speed, the blend space interpolating between them.
#   BS_TAG_Locomotion: standing: walk, jog, run, sprint forward; backpedals; strafes; diagonals; the standing idle at 0
#   BS_TAG_Crouch: the line: blocker's walk forward, crouched run, back jog, shuffles, back diagonals; the crouched idle
import math
import os

import unreal

A = "/Game/TAG/Anim/Authored/"
OUT_DIR = "/Game/TAG/Anim"
tools = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
opts = unreal.AnimPoseEvaluationOptions()


def load(name):
    a = unreal.load_asset(A + "A_TAG_" + name)
    if not a:
        unreal.log_warning("TAG anim missing: " + name)
    return a


def resample(bs):
    """Build the runtime blend data (the blend space editor UI normally does this)."""
    lib = getattr(unreal, "TagEditorLib", None)
    if lib is None:
        unreal.log_warning("TAG: TagEditorLib missing - build the C++ module first; blend space will not animate")
        return False
    return lib.resample_blend_space(bs)


def root_motion(seq):
    """(direction deg, speed cm/s) of a root-motion clip from its root bone's travel, in the blend space's convention:
    0 = forward, + to the right (the clips' root travels +Y forward, -X ... measured on import, see mm_loco_table)."""
    L = seq.get_play_length()
    p0 = unreal.AnimPoseExtensions.get_bone_pose(unreal.AnimPoseExtensions.get_anim_pose_at_time(seq, 0.0, opts), "root", unreal.AnimPoseSpaces.WORLD).translation
    p1 = unreal.AnimPoseExtensions.get_bone_pose(unreal.AnimPoseExtensions.get_anim_pose_at_time(seq, L, opts), "root", unreal.AnimPoseSpaces.WORLD).translation
    dx, dy = p1.x - p0.x, p1.y - p0.y
    sp = math.hypot(dx, dy) / max(L, 0.01)
    # clip space: +Y forward, +X left (a D3 strafe to the right travels -X)
    d = math.degrees(math.atan2(-dx, dy)) if sp > 5 else 0.0
    return d, sp


def make(path, name, idle, clips, grid_speed):
    # a fresh asset: an existing blend space kept its old sample slots past the new list (empty "Missing Sample
    # Animation" entries at runtime)
    if EAL.does_asset_exist(path) and not EAL.delete_asset(path):
        unreal.log_warning("TAG: could not delete " + path + "; rebuilding in place")
    if EAL.does_asset_exist(path):
        bs = unreal.load_asset(path)
        bs.set_editor_property("sample_data", [], unreal.PropertyAccessChangeNotifyMode.ALWAYS)
    else:
        fac = unreal.BlendSpaceFactoryNew()
        fac.set_editor_property("target_skeleton", idle.get_editor_property("skeleton"))
        bs = tools.create_asset(name, OUT_DIR, unreal.BlendSpace, fac)
    samples = []

    def add(seq, x, y):
        smp = unreal.BlendSample()
        smp.set_editor_property("animation", seq)
        smp.set_editor_property("sample_value", unreal.Vector(x, y, 0))
        smp.set_editor_property("rate_scale", 1.0)
        samples.append(smp)

    for x in (-180, -90, 0, 90, 180):
        add(idle, x, 0)
    top = 0.0
    rows = []
    for n in clips:
        seq = load(n)
        if not seq:
            continue
        d, sp = root_motion(seq)
        d = round(d / 45.0) * 45.0  # the clips are the pack's eight directions
        add(seq, d, sp)
        if abs(d) == 180:
            add(seq, -d, sp)
        top = max(top, sp)
        rows.append("%s %.0f deg %.0f cm/s" % (n, d, sp))
    params = bs.get_editor_property("blend_parameters")
    p0, p1 = params[0], params[1]
    p0.set_editor_property("display_name", "Direction")
    p0.set_editor_property("min", -180.0)
    p0.set_editor_property("max", 180.0)
    p0.set_editor_property("grid_num", 8)
    p1.set_editor_property("display_name", "Speed")
    p1.set_editor_property("min", 0.0)
    p1.set_editor_property("max", float(math.ceil(top)) + 1.0)  # the fastest clip inside the range (rounding down left it "missing")
    p1.set_editor_property("grid_num", grid_speed)
    params[0], params[1] = p0, p1
    bs.set_editor_property("blend_parameters", params, unreal.PropertyAccessChangeNotifyMode.ALWAYS)
    bs.set_editor_property("sample_data", samples, unreal.PropertyAccessChangeNotifyMode.ALWAYS)
    resample(bs)
    EAL.save_loaded_asset(bs)
    unreal.log("TAG_BS %s top %.0f cm/s samples %d: %s" % (name, top, len(samples), "; ".join(rows)))
    return top


if not EAL.does_directory_exist(OUT_DIR):
    EAL.make_directory(OUT_DIR)
stand = make(OUT_DIR + "/BS_TAG_Locomotion", "BS_TAG_Locomotion", load("Idle_Stand_Loop"),
             ["Loco_Walk_Loop", "Loco_Walk_Back", "Loco_Walk_Side", "Loco_Walk_Side_M",
              "Loco_Jog_Loop", "Loco_Jog_LB", "Loco_Run_Back", "Loco_Run_Side", "Loco_Run_Side_M", "Loco_Run_Diag", "Loco_Run_Diag_M",
              "Loco_Run_Average", "Loco_Run_Back_Big", "Loco_Sprint_Normal"], 4)
crouch = make(OUT_DIR + "/BS_TAG_Crouch", "BS_TAG_Crouch", load("Loco_Line_Idle_Crouched"),
              ["Loco_Line_Block_Walk", "Loco_Line_Shuffle_Walk", "Loco_Line_Shuffle_Walk_M", "Loco_Line_BackDiag_Walk", "Loco_Line_BackDiag_Walk_M",
               "Loco_Line_Back_Jog", "Loco_Line_Crouch_Run", "Loco_Line_Crouch_Run_Diag", "Loco_Line_Crouch_Run_Diag_M",
               "Loco_Line_Shuffle_Run", "Loco_Line_Shuffle_Run_M", "Loco_Line_Crouch_Back"], 2)
with open(os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "tag_anim_build.txt"), "w") as f:
    f.write("TAG locomotion top=%.1f crouch=%.1f\n" % (stand, crouch))
unreal.log("TAG anims built OK")
