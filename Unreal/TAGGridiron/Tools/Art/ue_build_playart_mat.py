# Unreal Editor Python: the play art material (routes, blocks, zones drawn on the field before the snap).
# Run: UnrealEditor-Cmd.exe TAGGridiron.uproject -run=pythonscript -script="<this file>"
#
# M_TagPlayArt: unlit translucent, two sided. Emissive = VertexColor.rgb * Intensity, Opacity = VertexColor.a * Opacity.
# The art is one procedural mesh with per-vertex colours (Render/PlayArt.cpp); Opacity fades it in and out.
import unreal

MAT_DIR = "/Game/TAG/Materials"
tools = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary


def new_material(name):
    path = MAT_DIR + "/" + name
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    return tools.create_asset(name, MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())


def expr(m, cls, x, y):
    return MEL.create_material_expression(m, cls, x, y)


def scalar(m, name, val, x, y):
    e = expr(m, unreal.MaterialExpressionScalarParameter, x, y)
    e.set_editor_property("parameter_name", name)
    e.set_editor_property("default_value", val)
    return e


def op(m, cls, a, b, x, y, oa="", ob=""):
    e = expr(m, cls, x, y)
    MEL.connect_material_expressions(a, oa, e, "A")
    MEL.connect_material_expressions(b, ob, e, "B")
    return e


def build_playart():
    m = new_material("M_TagPlayArt")
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property("two_sided", True)
    try:
        m.set_editor_property("translucency_pass", unreal.MaterialTranslucencyPass.MTP_AFTER_DOF)
    except Exception as e:  # property name differs between engine versions: the default pass is fine
        unreal.log_warning("translucency_pass: %s" % e)
    vc = expr(m, unreal.MaterialExpressionVertexColor, -700, 0)
    em = op(m, unreal.MaterialExpressionMultiply, vc, scalar(m, "Intensity", 2.0, -700, 200), -400, 0, "", "")
    op_ = op(m, unreal.MaterialExpressionMultiply, vc, scalar(m, "Opacity", 1.0, -700, 320), -400, 250, "A", "")
    MEL.connect_material_property(em, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(op_, "", unreal.MaterialProperty.MP_OPACITY)
    MEL.recompile_material(m)
    EAL.save_asset(m.get_path_name())
    unreal.log("TAG built M_TagPlayArt")


build_playart()
