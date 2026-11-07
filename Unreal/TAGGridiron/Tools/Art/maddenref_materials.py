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
import json
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


def build_master(path=MASTER, tiling=False):
    """M_MaddenRef_Ref for characters. M_MaddenRef_World adds a Tiling scalar on the UVs (world presets tile)."""
    if EAL.does_asset_exist(path):
        return unreal.load_asset(path)
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
    return m


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
    MEL.update_material_instance(mi)
    EAL.save_loaded_asset(mi, only_if_is_dirty=False)
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
    EAL.save_loaded_asset(mesh, only_if_is_dirty=False)
    return {"mi": b["mi"], "mesh": b["mesh"], "slots": list(slots)}


def build_preview(spec):
    """Level with every part as a SkeletalMeshActor at the origin. All parts share one skeleton and bind
    pose, so they line up into one dressed player without any animation setup."""
    path = spec["path"]
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if les is None or eas is None:
        return {"preview": "skipped, editor subsystems unavailable in this commandlet"}
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    les.new_level(path)
    for m in spec["meshes"]:
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
    les.save_current_level()
    return {"preview": path}


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
