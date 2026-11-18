# Unreal Editor Python: import RawArt textures and build TAG materials.
# Run: UnrealEditor-Cmd.exe TAGGridiron.uproject -run=pythonscript -script="<this file>"
import os
import unreal

PROJ_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
RAW = os.path.join(PROJ_DIR, "RawArt")
TEX_DIR = "/Game/TAG/Textures"
MAT_DIR = "/Game/TAG/Materials"
tools = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary


def import_tex(name, normal=False, srgb=True, gray=False):
    task = unreal.AssetImportTask()
    task.filename = os.path.join(RAW, name + ".png")
    task.destination_path = TEX_DIR
    task.destination_name = name
    task.replace_existing = True
    task.automated = True
    task.save = False
    tools.import_asset_tasks([task])
    tex = unreal.load_asset(TEX_DIR + "/" + name)
    if normal:
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        tex.set_editor_property("srgb", False)
    elif gray:
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_GRAYSCALE)
        tex.set_editor_property("srgb", False)
    else:
        tex.set_editor_property("srgb", srgb)
    EAL.save_loaded_asset(tex)
    return tex


def new_material(name):
    path = MAT_DIR + "/" + name
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    return tools.create_asset(name, MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())


def expr(mat, cls, x, y):
    return MEL.create_material_expression(mat, cls, x, y)


def scalar(mat, name, val, x, y):
    e = expr(mat, unreal.MaterialExpressionScalarParameter, x, y)
    e.set_editor_property("parameter_name", name)
    e.set_editor_property("default_value", val)
    return e


def vector(mat, name, val, x, y):
    e = expr(mat, unreal.MaterialExpressionVectorParameter, x, y)
    e.set_editor_property("parameter_name", name)
    e.set_editor_property("default_value", unreal.LinearColor(*val))
    return e


def texparam(mat, name, tex, x, y, normal=False, gray=False):
    e = expr(mat, unreal.MaterialExpressionTextureSampleParameter2D, x, y)
    e.set_editor_property("parameter_name", name)
    e.set_editor_property("texture", tex)
    if normal:
        e.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    elif gray:
        e.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE)
    return e


def op(mat, cls, a, b, x, y, oa="", ob=""):
    e = expr(mat, cls, x, y)
    MEL.connect_material_expressions(a, oa, e, "A")
    MEL.connect_material_expressions(b, ob, e, "B")
    return e


def build_turf(grass_d, grass_n, var, lines, wear):
    m = new_material("M_TagTurf")
    uv = expr(m, unreal.MaterialExpressionTextureCoordinate, -1400, 0)
    tiling = vector(m, "GrassTiling", (60.0, 27.0, 0, 0), -1400, 150)
    tmask = expr(m, unreal.MaterialExpressionComponentMask, -1250, 150)
    tmask.set_editor_property("r", True)
    tmask.set_editor_property("g", True)
    MEL.connect_material_expressions(tiling, "", tmask, "")
    guv = op(m, unreal.MaterialExpressionMultiply, uv, tmask, -1100, 50)
    gd = texparam(m, "Grass", grass_d, -900, -200)
    MEL.connect_material_expressions(guv, "", gd, "UVs")
    gn = texparam(m, "GrassNormal", grass_n, -900, 200, normal=True)
    MEL.connect_material_expressions(guv, "", gn, "UVs")
    # large-scale variation (wear / patchiness)
    vuv = op(m, unreal.MaterialExpressionMultiply, uv, scalar(m, "VariationTiling", 6.0, -1250, 400), -1100, 400)
    gv = texparam(m, "Variation", var, -900, 450, gray=True)
    MEL.connect_material_expressions(vuv, "", gv, "UVs")
    vlerp = expr(m, unreal.MaterialExpressionLinearInterpolate, -650, 450)
    vlerp.set_editor_property("const_a", 0.82)
    vlerp.set_editor_property("const_b", 1.12)
    MEL.connect_material_expressions(gv, "", vlerp, "Alpha")
    # mowing stripes: 24 bands of 5 yards along U
    u = expr(m, unreal.MaterialExpressionComponentMask, -1250, -450)
    u.set_editor_property("r", True)
    MEL.connect_material_expressions(uv, "", u, "")
    u24 = op(m, unreal.MaterialExpressionMultiply, u, scalar(m, "StripeCount", 12.0, -1250, -350), -1100, -450)
    fl = expr(m, unreal.MaterialExpressionFloor, -950, -450)
    MEL.connect_material_expressions(u24, "", fl, "")
    half = op(m, unreal.MaterialExpressionMultiply, fl, scalar(m, "Half", 0.5, -950, -350), -800, -450)
    fr = expr(m, unreal.MaterialExpressionFrac, -650, -450)
    MEL.connect_material_expressions(half, "", fr, "")
    stripe = expr(m, unreal.MaterialExpressionLinearInterpolate, -500, -450)
    stripe.set_editor_property("const_a", 1.06)
    stripe.set_editor_property("const_b", 0.88)
    MEL.connect_material_expressions(fr, "", stripe, "Alpha")
    stripe_amt = scalar(m, "StripeAmount", 1.0, -500, -330)
    stripe_mix = expr(m, unreal.MaterialExpressionLinearInterpolate, -350, -450)
    stripe_mix.set_editor_property("const_a", 1.0)
    MEL.connect_material_expressions(stripe, "", stripe_mix, "B")
    MEL.connect_material_expressions(stripe_amt, "", stripe_mix, "Alpha")
    tint = vector(m, "Tint", (1, 1, 1, 1), -650, -150)
    c1 = op(m, unreal.MaterialExpressionMultiply, gd, tint, -450, -200, "RGB")
    c2 = op(m, unreal.MaterialExpressionMultiply, c1, stripe_mix, -300, -250)
    c3 = op(m, unreal.MaterialExpressionMultiply, c2, vlerp, -150, -200)
    # painted surfaces (end zones): paint color modulated by grass luminance
    paint = vector(m, "PaintColor", (0.2, 0.2, 0.6, 1), -450, 0)
    lum = op(m, unreal.MaterialExpressionMultiply, gd, scalar(m, "PaintGrassMix", 2.6, -650, 80), -450, 100, "G")
    paint_tex = op(m, unreal.MaterialExpressionMultiply, paint, lum, -300, 50)
    paint_amt = scalar(m, "PaintAmount", 0.0, -300, 150)
    # season wear (T_TurfWear over the whole field, same UVs as the lines): grass thins to dirt between the hashes
    wt = texparam(m, "Wear", wear, -650, 1000, gray=True)
    MEL.connect_material_expressions(uv, "", wt, "UVs")
    wamt = op(m, unreal.MaterialExpressionMultiply, wt, scalar(m, "WearAmount", 0.0, -650, 1150), -450, 1000)
    dirt = op(m, unreal.MaterialExpressionMultiply, vector(m, "DirtColor", (0.16, 0.11, 0.065, 1), -650, 1250), lum, -450, 1200)
    worn = expr(m, unreal.MaterialExpressionLinearInterpolate, -150, 1000)
    MEL.connect_material_expressions(c3, "", worn, "A")
    MEL.connect_material_expressions(dirt, "", worn, "B")
    MEL.connect_material_expressions(wamt, "", worn, "Alpha")
    painted = expr(m, unreal.MaterialExpressionLinearInterpolate, 0, -100)
    MEL.connect_material_expressions(worn, "", painted, "A")
    MEL.connect_material_expressions(paint_tex, "", painted, "B")
    MEL.connect_material_expressions(paint_amt, "", painted, "Alpha")
    # yard-line paint overlay
    lt = texparam(m, "Lines", lines, -650, 650, gray=True)
    MEL.connect_material_expressions(uv, "", lt, "UVs")
    lamt = op(m, unreal.MaterialExpressionMultiply, lt, scalar(m, "LinesAmount", 1.0, -650, 800), -450, 700)
    line_col = vector(m, "LineColor", (0.78, 0.78, 0.76, 1), -300, 600)
    final = expr(m, unreal.MaterialExpressionLinearInterpolate, 150, 0)
    MEL.connect_material_expressions(painted, "", final, "A")
    MEL.connect_material_expressions(line_col, "", final, "B")
    MEL.connect_material_expressions(lamt, "", final, "Alpha")
    MEL.connect_material_property(final, "", unreal.MaterialProperty.MP_BASE_COLOR)
    rough = expr(m, unreal.MaterialExpressionLinearInterpolate, 150, 300)
    MEL.connect_material_expressions(scalar(m, "Roughness", 0.88, -50, 300), "", rough, "A")
    rough.set_editor_property("const_b", 0.55)
    MEL.connect_material_expressions(lamt, "", rough, "Alpha")
    MEL.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    nflat = expr(m, unreal.MaterialExpressionLinearInterpolate, 150, 450)
    MEL.connect_material_expressions(gn, "RGB", nflat, "A")
    nflat.set_editor_property("const_b", 0.0)
    flatn = vector(m, "FlatNormal", (0, 0, 1, 0), -50, 520)
    MEL.connect_material_expressions(flatn, "", nflat, "B")
    MEL.connect_material_expressions(lamt, "", nflat, "Alpha")
    MEL.connect_material_property(nflat, "", unreal.MaterialProperty.MP_NORMAL)
    MEL.connect_material_property(scalar(m, "Specular", 0.3, 150, 600), "", unreal.MaterialProperty.MP_SPECULAR)
    MEL.recompile_material(m)
    EAL.save_loaded_asset(m)


def build_crowd(crowd):
    m = new_material("M_TagCrowd")
    uv = expr(m, unreal.MaterialExpressionTextureCoordinate, -900, 0)
    tiling = vector(m, "Tiling", (8.0, 3.0, 0, 0), -900, 150)
    tm = expr(m, unreal.MaterialExpressionComponentMask, -750, 150)
    tm.set_editor_property("r", True)
    tm.set_editor_property("g", True)
    MEL.connect_material_expressions(tiling, "", tm, "")
    cuv = op(m, unreal.MaterialExpressionMultiply, uv, tm, -600, 50)
    t = texparam(m, "Crowd", crowd, -400, 0)
    MEL.connect_material_expressions(cuv, "", t, "UVs")
    team = vector(m, "TeamColor", (0.1, 0.2, 0.8, 1), -400, 250)
    shirt = op(m, unreal.MaterialExpressionMultiply, t, team, -200, 200, "RGB")
    col = expr(m, unreal.MaterialExpressionLinearInterpolate, 0, 0)
    MEL.connect_material_expressions(t, "RGB", col, "A")
    MEL.connect_material_expressions(shirt, "", col, "B")
    MEL.connect_material_expressions(t, "A", col, "Alpha")
    MEL.connect_material_property(col, "", unreal.MaterialProperty.MP_BASE_COLOR)
    emis = op(m, unreal.MaterialExpressionMultiply, col, scalar(m, "Glow", 0.12, -200, 400), 0, 300)
    MEL.connect_material_property(emis, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(scalar(m, "Roughness", 0.95, 0, 450), "", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.recompile_material(m)
    EAL.save_loaded_asset(m)


def main():
    if not EAL.does_directory_exist(TEX_DIR):
        EAL.make_directory(TEX_DIR)
    if not EAL.does_directory_exist(MAT_DIR):
        EAL.make_directory(MAT_DIR)
    gd = import_tex("T_TurfGrass_D")
    gn = import_tex("T_TurfGrass_N", normal=True)
    var = import_tex("T_TurfVariation", gray=True)
    lines = import_tex("T_FieldLines", gray=True)
    crowd = import_tex("T_Crowd")
    wear = import_tex("T_TurfWear", gray=True)
    build_turf(gd, gn, var, lines, wear)
    build_crowd(crowd)
    unreal.log("TAG materials built OK")


main()
