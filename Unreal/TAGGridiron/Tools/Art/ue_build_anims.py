# Unreal Editor Python: build TAG locomotion blend space from the Game Animation Sample mocap
# (UEFN mannequin skeleton). Run headless with -run=pythonscript.
import unreal

A = "/Game/Characters/UEFN_Mannequin/Animations/"
OUT_DIR = "/Game/TAG/Anim"
tools = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary


def load(path):
    a = unreal.load_asset(A + path)
    if not a:
        unreal.log_warning("TAG anim missing: " + path)
    return a


def resample(bs):
    """Build the runtime blend data (the blend space editor UI normally does this)."""
    lib = getattr(unreal, "TagEditorLib", None)
    if lib is None:
        unreal.log_warning("TAG: TagEditorLib missing - build the C++ module first; blend space will not animate")
        return False
    return lib.resample_blend_space(bs)


def speed_of(seq, fallback):
    """Average root-motion ground speed (cm/s) of a looping clip."""
    try:
        length = seq.get_play_length()
        a = unreal.AnimationLibrary.get_bone_pose_for_time(seq, "root", 0.0, False)
        b = unreal.AnimationLibrary.get_bone_pose_for_time(seq, "root", length - 1e-3, False)
        d = b.translation - a.translation
        sp = (d.x * d.x + d.y * d.y) ** 0.5 / max(length, 0.01)
        if sp > 1.0:
            return sp
    except Exception as e:  # API name differs between versions
        unreal.log_warning("TAG speed fallback for %s (%s)" % (seq.get_name(), e))
    return fallback


def build():
    if not EAL.does_directory_exist(OUT_DIR):
        EAL.make_directory(OUT_DIR)
    path = OUT_DIR + "/BS_TAG_Locomotion"
    idle = load("Idle/M_Neutral_Stand_Idle_Loop")
    skel = idle.get_editor_property("skeleton")
    if EAL.does_asset_exist(path):
        bs = unreal.load_asset(path)  # update in place (re-runs)
    else:
        fac = unreal.BlendSpaceFactoryNew()
        fac.set_editor_property("target_skeleton", skel)
        bs = tools.create_asset("BS_TAG_Locomotion", OUT_DIR, unreal.BlendSpace, fac)

    dirs = {"F": 0, "FR": 45, "RR": 90, "BR": 135, "B": 180, "BL": -135, "LL": -90, "FL": -45}
    samples = []

    def add(seq, x, y):
        if seq:
            smp = unreal.BlendSample()
            smp.set_editor_property("animation", seq)
            smp.set_editor_property("sample_value", unreal.Vector(x, y, 0))
            smp.set_editor_property("rate_scale", 1.0)
            samples.append(smp)

    for x in (-180, -90, 0, 90, 180):
        add(idle, x, 0)
    speeds = {}
    for gait, fb in (("Walk", 165.0), ("Run", 420.0)):
        fwd = load("%s/M_Neutral_%s_Loop_F" % (gait, gait))
        sp = speed_of(fwd, fb)
        speeds[gait] = sp
        for k, x in dirs.items():
            seq = load("%s/M_Neutral_%s_Loop_%s" % (gait, gait, k))
            add(seq, x, sp)
            if k == "B":
                add(seq, -180, sp)
    sprint_f = load("Sprint/M_Neutral_Sprint_Loop_F")
    ssp = speed_of(sprint_f, 680.0)
    speeds["Sprint"] = ssp
    add(sprint_f, 0, ssp)
    add(load("Sprint/M_Neutral_Sprint_Loop_FR"), 45, ssp)
    add(load("Sprint/M_Neutral_Sprint_Loop_FL"), -45, ssp)
    # no sprint backpedal: reuse run loops at the top speed row for wide angles
    for k, x in (("RR", 90), ("BR", 135), ("B", 180), ("BL", -135), ("LL", -90)):
        add(load("Run/M_Neutral_Run_Loop_%s" % k), x, ssp)
    add(load("Run/M_Neutral_Run_Loop_B"), -180, ssp)

    params = bs.get_editor_property("blend_parameters")
    p0 = params[0]
    p0.set_editor_property("display_name", "Direction")
    p0.set_editor_property("min", -180.0)
    p0.set_editor_property("max", 180.0)
    p0.set_editor_property("grid_num", 8)
    p1 = params[1]
    p1.set_editor_property("display_name", "Speed")
    p1.set_editor_property("min", 0.0)
    p1.set_editor_property("max", float(round(ssp)))
    p1.set_editor_property("grid_num", 4)
    params[0] = p0
    params[1] = p1
    bs.set_editor_property("blend_parameters", params, unreal.PropertyAccessChangeNotifyMode.ALWAYS)
    bs.set_editor_property("sample_data", samples, unreal.PropertyAccessChangeNotifyMode.ALWAYS)
    resample(bs)
    EAL.save_loaded_asset(bs)
    msg = "TAG locomotion speeds walk=%.1f run=%.1f sprint=%.1f samples=%d" % (speeds["Walk"], speeds["Run"], speeds["Sprint"], len(samples))
    unreal.log(msg)
    import os
    with open(os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "tag_anim_build.txt"), "w") as f:
        f.write(msg + "\n")


def build_crouch():
    path = OUT_DIR + "/BS_TAG_Crouch"
    idle = load("Idle/M_Neutral_Crouch_Idle_Loop")
    skel = idle.get_editor_property("skeleton")
    if EAL.does_asset_exist(path):
        bs = unreal.load_asset(path)
    else:
        fac = unreal.BlendSpaceFactoryNew()
        fac.set_editor_property("target_skeleton", skel)
        bs = tools.create_asset("BS_TAG_Crouch", OUT_DIR, unreal.BlendSpace, fac)
    samples = []

    def add(seq, x, y):
        if seq:
            smp = unreal.BlendSample()
            smp.set_editor_property("animation", seq)
            smp.set_editor_property("sample_value", unreal.Vector(x, y, 0))
            smp.set_editor_property("rate_scale", 1.0)
            samples.append(smp)

    for x in (-180, -90, 0, 90, 180):
        add(idle, x, 0)
    fwd = load("Crouch/M_Neutral_Crouch_Loop_F")
    sp = speed_of(fwd, 150.0)
    dirs = {"F": 0, "FR": 45, "RR": 90, "BR": 135, "B": 180, "BL": -135, "LL": -90, "FL": -45}
    for k, x in dirs.items():
        seq = load("Crouch/M_Neutral_Crouch_Loop_%s" % k)
        add(seq, x, sp)
        if k == "B":
            add(seq, -180, sp)
    params = bs.get_editor_property("blend_parameters")
    p0 = params[0]
    p0.set_editor_property("display_name", "Direction")
    p0.set_editor_property("min", -180.0)
    p0.set_editor_property("max", 180.0)
    p0.set_editor_property("grid_num", 8)
    p1 = params[1]
    p1.set_editor_property("display_name", "Speed")
    p1.set_editor_property("min", 0.0)
    p1.set_editor_property("max", float(round(sp)))
    p1.set_editor_property("grid_num", 2)
    params[0] = p0
    params[1] = p1
    bs.set_editor_property("blend_parameters", params, unreal.PropertyAccessChangeNotifyMode.ALWAYS)
    bs.set_editor_property("sample_data", samples, unreal.PropertyAccessChangeNotifyMode.ALWAYS)
    resample(bs)
    EAL.save_loaded_asset(bs)
    return sp


build()
csp = build_crouch()
import os
with open(os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "tag_anim_build.txt"), "a") as f:
    f.write("crouch=%.1f\n" % csp)
unreal.log("TAG anims built OK")
