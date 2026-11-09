# Unreal Editor Python: stadium display materials (LED ribbon boards, video boards, suite glass).
# Run: UnrealEditor-Cmd.exe TAGGridiron.uproject -run=pythonscript -script="<this file>"
#
# M_TagLED: emissive panel. Emissive = lerp(Color, ColorB, segment) * Intensity, where the segment alternates every
# 1/Count of U and scrolls at Speed (ribbon boards run team-colour panels around the bowl); Count 0 is a solid panel.
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


def vector(m, name, val, x, y):
    e = expr(m, unreal.MaterialExpressionVectorParameter, x, y)
    e.set_editor_property("parameter_name", name)
    e.set_editor_property("default_value", unreal.LinearColor(*val))
    return e


def op(m, cls, a, b, x, y, oa="", ob=""):
    e = expr(m, cls, x, y)
    MEL.connect_material_expressions(a, oa, e, "A")
    MEL.connect_material_expressions(b, ob, e, "B")
    return e


def build_led():
    m = new_material("M_TagLED")
    uv = expr(m, unreal.MaterialExpressionTextureCoordinate, -1200, 0)
    u = expr(m, unreal.MaterialExpressionComponentMask, -1050, 0)
    u.set_editor_property("r", True)
    MEL.connect_material_expressions(uv, "", u, "")
    uc = op(m, unreal.MaterialExpressionMultiply, u, scalar(m, "Count", 0.0, -1050, 120), -900, 0)
    t = expr(m, unreal.MaterialExpressionTime, -1050, 250)
    ts = op(m, unreal.MaterialExpressionMultiply, t, scalar(m, "Speed", 0.0, -1050, 350), -900, 250)
    s = op(m, unreal.MaterialExpressionAdd, uc, ts, -750, 100)
    fr = expr(m, unreal.MaterialExpressionFrac, -600, 100)
    MEL.connect_material_expressions(s, "", fr, "")
    seg = expr(m, unreal.MaterialExpressionRound, -450, 100)
    MEL.connect_material_expressions(fr, "", seg, "")
    col = expr(m, unreal.MaterialExpressionLinearInterpolate, -250, 0)
    MEL.connect_material_expressions(vector(m, "Color", (0.1, 0.2, 0.8, 1), -450, -150), "", col, "A")
    MEL.connect_material_expressions(vector(m, "ColorB", (0.1, 0.2, 0.8, 1), -450, -50), "", col, "B")
    MEL.connect_material_expressions(seg, "", col, "Alpha")
    emis = op(m, unreal.MaterialExpressionMultiply, col, scalar(m, "Intensity", 4.0, -250, 200), -50, 100)
    MEL.connect_material_property(emis, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    base = op(m, unreal.MaterialExpressionMultiply, col, scalar(m, "Diffuse", 0.08, -250, 300), -50, 250)
    MEL.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(scalar(m, "Roughness", 0.35, -50, 400), "", unreal.MaterialProperty.MP_ROUGHNESS)
    m.set_editor_property("two_sided", True)
    MEL.recompile_material(m)
    EAL.save_loaded_asset(m)


build_led()
unreal.log("TAG stadium materials built OK")
