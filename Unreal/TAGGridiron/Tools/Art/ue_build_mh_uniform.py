# Unreal Editor Python (headless): build M_TagMHUniform - paints a football uniform onto a MetaHuman
# body by region (bind-pose position), keeping the baked MetaHuman skin on arms/neck.
# Region boundaries are per-body scalar parameters filled in at runtime from the skeleton.
import os
import unreal

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
OUT_DIR = "/Game/TAG/Materials"
NAME = "M_TagMHUniform"
SKIN = "/Game/TAG/MetaHumans/Unpacked/TAG_Lineman/Body/Baked/"
LOG = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "tag_mh_uniform.txt")

REGION_HLSL = r"""
float ax = abs(P.x);
float z = P.z;
float3 c = Skin.rgb;
float cloth = 0;
if (z < ShoeZ) { c = Shoe.rgb; cloth = 1; }
else if (z < KneeZ && ax < LegX) { c = Socks.rgb; cloth = 1; }
else if (z < WaistZ && ax < LegX)
{
    c = Pants.rgb; cloth = 1;
    if (ax > ThighX && abs(P.y - LegY) < 1.7) c = Trim.rgb;           // side stripe
    if (z > WaistZ - 3.0) c = lerp(Pants.rgb, float3(0.02,0.02,0.02), 0.85); // belt
}
else if (z >= WaistZ && z < NeckZ && ax < SleeveX)
{
    c = Jersey.rgb; cloth = 1;
    if (ax > SleeveX - 4.0 && ax < SleeveX - 1.4) c = Trim.rgb;        // sleeve stripe
    if (z > NeckZ - 2.4) c = Trim.rgb;                                 // collar
}
else if (ax > HandX) { c = Glove.rgb; cloth = 0.6; }
return float4(c, cloth);
"""

SCALARS = {"WaistZ": 110.0, "NeckZ": 160.0, "KneeZ": 48.0, "ShoeZ": 12.0, "SleeveX": 32.0,
           "LegX": 26.0, "ThighX": 10.0, "LegY": 0.0, "HandX": 62.0}
VECTORS = {"Jersey": (0.6, 0.05, 0.05), "Pants": (0.9, 0.9, 0.9), "Trim": (0.95, 0.75, 0.1),
           "Socks": (0.6, 0.05, 0.05), "Shoe": (0.02, 0.02, 0.02), "Glove": (0.03, 0.03, 0.03)}


def build():
    if not EAL.does_directory_exist(OUT_DIR):
        EAL.make_directory(OUT_DIR)
    path = OUT_DIR + "/" + NAME
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    m = tools.create_asset(NAME, OUT_DIR, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("used_with_skeletal_mesh", True)

    y = 0

    def node(cls, x=-900):
        nonlocal y
        e = MEL.create_material_expression(m, cls, x, y)
        y += 140
        return e

    pos = node(unreal.MaterialExpressionPreSkinnedPosition)

    bc = node(unreal.MaterialExpressionTextureSampleParameter2D)
    bc.set_editor_property("parameter_name", "SkinBC")
    bc.set_editor_property("texture", unreal.load_asset(SKIN + "T_Body_BC"))
    bc.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)

    nm = node(unreal.MaterialExpressionTextureSampleParameter2D)
    nm.set_editor_property("parameter_name", "SkinN")
    nm.set_editor_property("texture", unreal.load_asset(SKIN + "T_Body_N"))
    nm.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)

    custom = MEL.create_material_expression(m, unreal.MaterialExpressionCustom, -450, 0)
    custom.set_editor_property("code", REGION_HLSL)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    custom.set_editor_property("description", "TAG uniform regions")
    names = ["P", "Skin"] + list(VECTORS.keys()) + list(SCALARS.keys())
    ins = []
    for n in names:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        ins.append(ci)
    custom.set_editor_property("inputs", ins)

    # pre-skinned (bind pose) position is vertex-shader only: hand it to the pixel shader
    vi = node(unreal.MaterialExpressionVertexInterpolator, -700)
    MEL.connect_material_expressions(pos, "", vi, "")
    MEL.connect_material_expressions(vi, "", custom, "P")
    MEL.connect_material_expressions(bc, "RGB", custom, "Skin")
    for k, v in VECTORS.items():
        e = node(unreal.MaterialExpressionVectorParameter)
        e.set_editor_property("parameter_name", k)
        e.set_editor_property("default_value", unreal.LinearColor(v[0], v[1], v[2], 1))
        MEL.connect_material_expressions(e, "", custom, k)
    for k, v in SCALARS.items():
        e = node(unreal.MaterialExpressionScalarParameter)
        e.set_editor_property("parameter_name", k)
        e.set_editor_property("default_value", v)
        MEL.connect_material_expressions(e, "", custom, k)

    rgb = MEL.create_material_expression(m, unreal.MaterialExpressionComponentMask, -200, -100)
    for ch, on in (("r", True), ("g", True), ("b", True), ("a", False)):
        rgb.set_editor_property(ch, on)
    MEL.connect_material_expressions(custom, "", rgb, "")
    cloth = MEL.create_material_expression(m, unreal.MaterialExpressionComponentMask, -200, 60)
    for ch, on in (("r", False), ("g", False), ("b", False), ("a", True)):
        cloth.set_editor_property(ch, on)
    MEL.connect_material_expressions(custom, "", cloth, "")
    MEL.connect_material_property(rgb, "", unreal.MaterialProperty.MP_BASE_COLOR)

    def const(v, x, yy):
        c = MEL.create_material_expression(m, unreal.MaterialExpressionConstant, x, yy)
        c.set_editor_property("r", v)
        return c

    def lerp(a, b, alpha, x, yy, a_out="", b_out=""):
        l = MEL.create_material_expression(m, unreal.MaterialExpressionLinearInterpolate, x, yy)
        MEL.connect_material_expressions(a, a_out, l, "A")
        MEL.connect_material_expressions(b, b_out, l, "B")
        MEL.connect_material_expressions(alpha, "", l, "Alpha")
        return l

    rough = lerp(const(0.48, -300, 200), const(0.74, -300, 260), cloth, -100, 220)
    MEL.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    spec = lerp(const(0.42, -300, 320), const(0.28, -300, 380), cloth, -100, 340)
    MEL.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)
    flat = MEL.create_material_expression(m, unreal.MaterialExpressionConstant3Vector, -300, 440)
    flat.set_editor_property("constant", unreal.LinearColor(0, 0, 1, 1))
    nrm = lerp(nm, flat, cloth, -100, 460, a_out="RGB")
    MEL.connect_material_property(nrm, "", unreal.MaterialProperty.MP_NORMAL)

    MEL.recompile_material(m)
    EAL.save_loaded_asset(m)
    return m


try:
    mat = build()
    stats = ""
    try:
        st = MEL.get_statistics(mat)
        stats = " vs=%s ps=%s" % (st.num_vertex_shader_instructions, st.num_pixel_shader_instructions)
    except Exception as e:
        stats = " (stats n/a: %s)" % e
    msg = "OK " + NAME + stats
except Exception as e:
    import traceback
    msg = "FAIL " + traceback.format_exc()
with open(LOG, "w") as f:
    f.write(msg + "\n")
