# Unreal Editor Python. Builds reference materials for /Game/MaddenRef from a JSON description.
# Run headless:
#   UnrealEditor-Cmd.exe TAGGridiron.uproject -run=pythonscript -script="<this file>" -unattended -nosplash -nullrhi
# Env MADDENREF_MATERIALS names the JSON. Env MADDENREF_REPORT names the report written back.
#
# JSON
#   {"bindings": [{"mesh": "/Game/MaddenRef/...", "slot": 0 | "all", "mi": "/Game/MaddenRef/.../MI_x",
#                  "textures": {"BaseColor": "/Game/...", "Normal": "/Game/...", "RSM": "/Game/..."},
#                  "scalars": {"Roughness": 0.6}, "switches": {"UseRSM": true, "UseAlphaMask": false}}],
#    "preview_level": {"path": "/Game/MaddenRef/Maps/L_x",
#                      "meshes": ["/Game/MaddenRef/..." | {"mesh": "/Game/...", "x": 120.0, "label": "thin"}]}}
#
# Master material M_MaddenRef_Ref (rebuilt only when missing)
#   BaseColor texture -> Base Color, Normal texture -> Normal (already DirectX, converted by texfix.py).
#   UseRSM switch. When on, RSM texture R -> Roughness, G -> Specular, B -> Metallic.
#   RSMIsSkin switch. Skin maps are R_S_SSR_T (B is subsurface), so Metallic falls back to the scalar.
#   When UseRSM is off, the Roughness, Specular and Metallic scalars are used.
#   Tint vector (default white) multiplies Base Color. Pieces with only a colour in their Madden preset (facemasks)
#   use it over the white default texture. Masters built before Tint existed are upgraded in place.
import json
import math
import os
import traceback

import unreal

ROOT = "/Game/MaddenRef"
MASTER = ROOT + "/Materials/M_MaddenRef_Ref"
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
WHITE = "/Engine/EngineResources/WhiteSquareTexture"
FLAT_NORMAL = "/Engine/EngineMaterials/DefaultNormal"


def _expr(m, cls, x, y):
    return MEL.create_material_expression(m, cls, x, y)


def _tex(m, name, default, x, y, sampler):
    e = _expr(m, unreal.MaterialExpressionTextureSampleParameter2D, x, y)
    e.set_editor_property("parameter_name", name)
    e.set_editor_property("texture", unreal.load_asset(default))
    e.set_editor_property("sampler_type", sampler)
    return e


def _scalar(m, name, val, x, y):
    e = _expr(m, unreal.MaterialExpressionScalarParameter, x, y)
    e.set_editor_property("parameter_name", name)
    e.set_editor_property("default_value", val)
    return e


def _switch(m, name, x, y):
    e = _expr(m, unreal.MaterialExpressionStaticSwitchParameter, x, y)
    e.set_editor_property("parameter_name", name)
    e.set_editor_property("default_value", False)
    return e


WORLD_MASTER = ROOT + "/Materials/M_MaddenRef_World"


def ensure_tint(m):
    """Adds the Tint vector parameter between the BaseColor texture and Base Color, once. Default white keeps
    every existing instance as it was."""
    if "Tint" in [str(n) for n in MEL.get_vector_parameter_names(m)]:
        return m
    base = MEL.get_material_property_input_node(m, unreal.MaterialProperty.MP_BASE_COLOR)
    if base is None:
        raise RuntimeError("master has no Base Color input " + m.get_path_name())
    tint = _expr(m, unreal.MaterialExpressionVectorParameter, -800, -550)
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    mask = _expr(m, unreal.MaterialExpressionComponentMask, -550, -550)
    for c in ("r", "g", "b"):
        mask.set_editor_property(c, True)
    mask.set_editor_property("a", False)
    mul = _expr(m, unreal.MaterialExpressionMultiply, -300, -400)
    MEL.connect_material_expressions(tint, "", mask, "")
    MEL.connect_material_expressions(base, "RGB", mul, "A")
    MEL.connect_material_expressions(mask, "", mul, "B")
    MEL.connect_material_property(mul, "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.recompile_material(m)
    if not EAL.save_loaded_asset(m, only_if_is_dirty=False):
        raise RuntimeError("save failed for %s (file locked, is the Unreal Editor open?)" % m.get_path_name())
    return m


def build_master(path=MASTER, tiling=False):
    """M_MaddenRef_Ref for characters. M_MaddenRef_World adds a Tiling scalar on the UVs (world presets tile)."""
    if EAL.does_asset_exist(path):
        m = unreal.load_asset(path)
        return ensure_tint(m) if path == MASTER else m
    pkg, name = path.rsplit("/", 1)
    m = tools.create_asset(name, pkg, unreal.Material, unreal.MaterialFactoryNew())
    base = _tex(m, "BaseColor", WHITE, -800, -300, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    nrm = _tex(m, "Normal", FLAT_NORMAL, -800, 0, unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    rsm = _tex(m, "RSM", WHITE, -800, 300, unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    if tiling:
        uv = _expr(m, unreal.MaterialExpressionTextureCoordinate, -1300, 0)
        tile = _scalar(m, "Tiling", 1.0, -1300, 150)
        mul = _expr(m, unreal.MaterialExpressionMultiply, -1100, 0)
        MEL.connect_material_expressions(uv, "", mul, "A")
        MEL.connect_material_expressions(tile, "", mul, "B")
        for t in (base, nrm, rsm):
            MEL.connect_material_expressions(mul, "", t, "UVs")
    rough = _scalar(m, "Roughness", 0.6, -500, 500)
    metal = _scalar(m, "Metallic", 0.0, -500, 600)
    spec = _scalar(m, "Specular", 0.5, -500, 700)
    sw_r = _switch(m, "UseRSM", -250, 300)
    sw_s = _switch(m, "UseRSM", -250, 450)
    sw_m = _switch(m, "UseRSM", -250, 600)
    sw_skin = _switch(m, "RSMIsSkin", -500, 850)
    MEL.connect_material_expressions(rsm, "R", sw_r, "True")
    MEL.connect_material_expressions(rough, "", sw_r, "False")
    MEL.connect_material_expressions(rsm, "G", sw_s, "True")
    MEL.connect_material_expressions(spec, "", sw_s, "False")
    MEL.connect_material_expressions(rsm, "B", sw_skin, "False")
    MEL.connect_material_expressions(metal, "", sw_skin, "True")
    MEL.connect_material_expressions(sw_skin, "", sw_m, "True")
    MEL.connect_material_expressions(metal, "", sw_m, "False")
    MEL.connect_material_property(base, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(nrm, "RGB", unreal.MaterialProperty.MP_NORMAL)
    MEL.connect_material_property(sw_r, "", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.connect_material_property(sw_s, "", unreal.MaterialProperty.MP_SPECULAR)
    MEL.connect_material_property(sw_m, "", unreal.MaterialProperty.MP_METALLIC)
    MEL.recompile_material(m)
    EAL.save_loaded_asset(m, only_if_is_dirty=False)
    return ensure_tint(m) if path == MASTER else m


def make_mi(path, master):
    if EAL.does_asset_exist(path):
        mi = unreal.load_asset(path)
    else:
        pkg, name = path.rsplit("/", 1)
        mi = tools.create_asset(name, pkg, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(mi, master)
    return mi


def apply_binding(b, master):
    if b.get("master") == WORLD_MASTER:
        master = build_master(WORLD_MASTER, tiling=True)
    mi = make_mi(b["mi"], master)
    for k, v in b.get("textures", {}).items():
        tex = unreal.load_asset(v)
        if tex is None:
            raise RuntimeError("missing texture " + v)
        MEL.set_material_instance_texture_parameter_value(mi, k, tex)
    for k, v in b.get("scalars", {}).items():
        MEL.set_material_instance_scalar_parameter_value(mi, k, float(v))
    for k, v in b.get("switches", {}).items():
        MEL.set_material_instance_static_switch_parameter_value(mi, k, bool(v))
    for k, v in b.get("vectors", {}).items():
        MEL.set_material_instance_vector_parameter_value(mi, k, unreal.LinearColor(*[float(x) for x in (list(v) + [1.0] * 4)[:4]]))
    MEL.update_material_instance(mi)
    if not EAL.save_loaded_asset(mi, only_if_is_dirty=False):
        raise RuntimeError("save failed for %s (file locked, is the Unreal Editor open?)" % b["mi"])
    mesh = unreal.load_asset(b["mesh"])
    if mesh is None:
        raise RuntimeError("missing mesh " + b["mesh"])
    mats = list(mesh.materials) if isinstance(mesh, unreal.SkeletalMesh) else list(mesh.static_materials)
    slots = range(len(mats)) if b.get("slot", 0) == "all" else [int(b.get("slot", 0))]
    if any(i >= len(mats) for i in slots):
        raise IndexError("slot %s out of range, mesh has %d material slots" % (list(slots), len(mats)))
    for i in slots:
        mats[i].material_interface = mi
    if isinstance(mesh, unreal.SkeletalMesh):
        mesh.set_editor_property("materials", mats)
    else:
        mesh.set_editor_property("static_materials", mats)
    if not EAL.save_loaded_asset(mesh, only_if_is_dirty=False):
        raise RuntimeError("save failed for %s (file locked, is the Unreal Editor open?)" % b["mesh"])
    return {"mi": b["mi"], "mesh": b["mesh"], "slots": list(slots)}


HFOV = 90.0          # camera horizontal field of view
ASPECT = 16.0 / 9.0
FILL = 1.0 / 3.0     # share of the view height the subject should fill


def _bounds_z(mesh):
    if isinstance(mesh, unreal.StaticMesh):
        b = mesh.get_bounding_box()
        return b.min.z, b.max.z
    b = mesh.get_bounds()
    return b.origin.z - b.box_extent.z, b.origin.z + b.box_extent.z


def _forward(meshes):
    """Mesh forward axis in Unreal space, from the skeleton's reference pose (toe ahead of ankle)."""
    for mesh in meshes:
        sk = mesh.skeleton if isinstance(mesh, unreal.SkeletalMesh) else None
        if sk is None:
            continue
        try:
            pose = unreal.AnimPoseExtensions.get_reference_pose(sk)
            toe = unreal.AnimPoseExtensions.get_bone_pose(pose, "LeftToeBase", unreal.AnimPoseSpaces.WORLD).translation
            foot = unreal.AnimPoseExtensions.get_bone_pose(pose, "LeftFoot", unreal.AnimPoseSpaces.WORLD).translation
            fx, fy = toe.x - foot.x, toe.y - foot.y
            n = math.hypot(fx, fy)
            if n > 1e-3:
                return fx / n, fy / n
        except Exception:
            pass
    return 1.0, 0.0


def _component(actor, cls):
    return actor.get_component_by_class(cls)


def stage_level(placed, staging):
    """Lights, a grid ground plane, the GameModeBase override, and a camera where the subject's height fills
    about a third of the view. placed = [(mesh, x, y, group)]."""
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    info = {}
    # GameModeBase override, so the preview never runs the game's own GridironGameMode
    ws = ues.get_editor_world().get_world_settings()
    ws.set_editor_property("default_game_mode", unreal.GameModeBase.static_class())
    info["game_mode"] = "GameModeBase"
    if not placed:
        return info
    # groups (one dressed player, one helmet model, ...) and the subject height
    groups = {}
    for mesh, x, y, g in placed:
        lo, hi = _bounds_z(mesh)
        e = groups.setdefault(g, {"x": x, "y": y, "lo": lo, "hi": hi})
        e["lo"], e["hi"] = min(e["lo"], lo), max(e["hi"], hi)
    fx, fy = _forward([p[0] for p in placed])
    c45 = math.cos(math.radians(45))
    dx, dy = fx * c45 - fy * c45, fx * c45 + fy * c45          # three quarter view of the players' front
    rows = {}
    for g in groups.values():
        rows.setdefault(round(g["y"], 1), []).append(g)
    row = max(rows.values(), key=lambda r: sum(g["x"] * dx + g["y"] * dy for g in r) / len(r))
    tx = sum(g["x"] for g in row) / len(row)
    ty = sum(g["y"] for g in row) / len(row)
    lo = min(g["lo"] for g in row)
    height = float(staging.get("subject_height_cm") or max(g["hi"] - g["lo"] for g in row))
    tz = lo + 0.5 * height
    vfov_half = math.atan(math.tan(math.radians(HFOV / 2)) / ASPECT)
    dist = height / (FILL * 2.0 * math.tan(vfov_half))
    cam = unreal.Vector(tx + dx * dist, ty + dy * dist, tz + 0.15 * height)
    target = unreal.Vector(tx, ty, tz)
    rot = unreal.MathLibrary.find_look_at_rotation(cam, target)
    camera = eas.spawn_actor_from_class(unreal.CameraActor, cam, rot)
    camera.set_actor_label("PreviewCamera")
    cc = _component(camera, unreal.CameraComponent)
    cc.set_editor_property("field_of_view", HFOV)
    cc.set_editor_property("aspect_ratio", ASPECT)
    cc.set_editor_property("constrain_aspect_ratio", False)
    camera.set_editor_property("auto_activate_for_player", unreal.AutoReceiveInput.PLAYER0)
    ps = eas.spawn_actor_from_class(unreal.PlayerStart, cam, unreal.Rotator(0.0, 0.0, rot.yaw))
    ps.set_actor_label("PreviewPlayerStart")
    info.update({"subject_height_cm": round(height, 1), "camera_distance_cm": round(dist, 1),
                 "camera": [round(cam.x, 1), round(cam.y, 1), round(cam.z, 1)], "target": [round(tx, 1), round(ty, 1), round(tz, 1)],
                 "forward": [round(fx, 3), round(fy, 3)]})
    # grid ground plane under everything (and under the camera)
    xs = [p[1] for p in placed] + [cam.x]
    ys = [p[2] for p in placed] + [cam.y]
    margin = max(500.0, 2.0 * height)
    sx, sy = (max(xs) - min(xs)) + 2 * margin, (max(ys) - min(ys)) + 2 * margin
    plane = eas.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector((max(xs) + min(xs)) / 2, (max(ys) + min(ys)) / 2, 0.0))
    plane.set_actor_label("PreviewGround")
    pc = plane.static_mesh_component
    pc.set_static_mesh(unreal.load_asset("/Engine/BasicShapes/Plane"))
    pc.set_material(0, unreal.load_asset("/Engine/EngineMaterials/WorldGridMaterial"))
    plane.set_actor_scale3d(unreal.Vector(sx / 100.0, sy / 100.0, 1.0))
    info["ground_m"] = [round(sx / 100.0, 1), round(sy / 100.0, 1)]
    # lights: atmosphere, a movable sun from the camera side, and a real time sky light (no lighting build needed)
    eas.spawn_actor_from_class(unreal.SkyAtmosphere, unreal.Vector(0, 0, 0)).set_actor_label("PreviewSkyAtmosphere")
    sun = eas.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(tx, ty, tz + 500), unreal.Rotator(0.0, -40.0, rot.yaw - 30.0))
    sun.set_actor_label("PreviewSun")
    sun.root_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    lc = _component(sun, unreal.DirectionalLightComponent)
    lc.set_editor_property("atmosphere_sun_light", True)
    sky = eas.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(tx, ty, tz + 300))
    sky.set_actor_label("PreviewSkyLight")
    sky.root_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    _component(sky, unreal.SkyLightComponent).set_editor_property("real_time_capture", True)
    info["lights"] = ["SkyAtmosphere", "DirectionalLight (movable)", "SkyLight (real time capture)"]
    return info


def build_preview(spec):
    """Preview level. Every part as a SkeletalMeshActor (all parts share one skeleton and bind pose, so they
    line up into one dressed player), then staged with lights, ground, camera and the GameModeBase override."""
    path = spec["path"]
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if les is None or eas is None:
        return {"preview": "skipped, editor subsystems unavailable in this commandlet"}
    # Reuse an existing level in place. Deleting it and calling new_level on the same path fails silently
    # in one commandlet session (the deleted package stays in memory), which left old levels unchanged.
    if EAL.does_asset_exist(path):
        if not les.load_level(path):
            raise RuntimeError("could not load " + path)
        old = [a for a in eas.get_all_level_actors() if not isinstance(a, unreal.WorldSettings)]
        if old:
            eas.destroy_actors(old)
    elif not les.new_level(path):
        raise RuntimeError("could not create " + path)
    placed = []
    for i, m in enumerate(spec["meshes"]):
        if isinstance(m, dict):
            mpath, x, y, label = m["mesh"], float(m.get("x", 0.0)), float(m.get("y", 0.0)), m.get("label", "")
        else:
            mpath, x, y, label = m, 0.0, 0.0, ""
        mesh = unreal.load_asset(mpath)
        if mesh is None:
            continue
        if isinstance(mesh, unreal.StaticMesh):
            actor = eas.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(x, y, 0))
            actor.static_mesh_component.set_static_mesh(mesh)
        else:
            actor = eas.spawn_actor_from_class(unreal.SkeletalMeshActor, unreal.Vector(x, y, 0))
            comp = actor.skeletal_mesh_component
            comp.set_skeletal_mesh_asset(mesh)
            if isinstance(m, dict) and m.get("anim"):
                # Every part plays the same sequence from the same start, so the dressed player stays in sync.
                anim = unreal.load_asset(m["anim"])
                if anim is not None:
                    comp.set_animation_mode(unreal.AnimationMode.ANIMATION_SINGLE_NODE)
                    comp.set_animation(anim)
                    comp.set_editor_property("animation_data", unreal.SingleAnimationPlayData(anim_to_play=anim, saved_looping=True, saved_playing=True))
        actor.set_actor_label((label + "_" if label else "") + mpath.rsplit("/", 1)[1])
        # parts at the same spot without a group or label form one subject (one dressed player)
        group = m.get("group", label or (x, y)) if isinstance(m, dict) else (x, y)
        placed.append((mesh, x, y, group))
    staged = stage_level(placed, spec.get("staging", {}))
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    if world.get_outermost().get_name() != path:
        raise RuntimeError("current level is %s, expected %s" % (world.get_outermost().get_name(), path))
    if not les.save_current_level():
        raise RuntimeError("save failed for " + path)
    return {"preview": path, "staging": staged, "actors": len(eas.get_all_level_actors())}


def run():
    unreal.AssetRegistryHelpers.get_asset_registry().search_all_assets(True)
    spec = json.load(open(os.environ["MADDENREF_MATERIALS"]))
    report = {"results": []}
    master = build_master()
    for b in spec.get("bindings", []):
        try:
            if not b["mi"].startswith(ROOT) or not b["mesh"].startswith(ROOT):
                raise ValueError("outside " + ROOT)
            r = apply_binding(b, master)
            r["status"] = "ok"
        except Exception as e:
            r = {"status": "fail", "binding": b, "error": repr(e), "trace": traceback.format_exc()[-800:]}
        report["results"].append(r)
    if spec.get("preview_level"):
        try:
            report["preview"] = build_preview(spec["preview_level"])
        except Exception as e:
            report["preview"] = {"status": "fail", "error": repr(e), "trace": traceback.format_exc()[-800:]}
    with open(os.environ["MADDENREF_REPORT"], "w") as fh:
        json.dump(report, fh, indent=1)


run()
