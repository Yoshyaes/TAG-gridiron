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
if (z < ShoeZ) { c = Shoe.rgb; cloth = 0.96; }
else if (z < KneeZ && ax < LegX) { c = Socks.rgb; cloth = 0.97; }
else if (z < WaistZ && ax < LegX)
{
    c = Pants.rgb; cloth = 0.98;
    if (ax > ThighX && abs(P.y - LegY) < 1.7) c = Trim.rgb;           // side stripe
    if (z > WaistZ - 3.0) c = lerp(Pants.rgb, float3(0.02,0.02,0.02), 0.85); // belt
}
else if (z >= WaistZ && z < NeckZ && ax < SleeveX)
{
    c = Jersey.rgb; cloth = 1;
    if (ax > SleeveX - 4.0 && ax < SleeveX - 1.4) c = Trim.rgb;        // sleeve stripe
    if (z > NeckZ - 2.4) c = Trim.rgb;                                 // collar
    // numbers on chest and back: TAG digit atlas (R fill, G outline), number colour with a trim outline
    float side = P.y - TorsoY;
    if (abs(side) > 4.0 && ax < SleeveX - 6.0)
    {
        bool front = side > 0.0;
        float H = front ? 16.0 : 21.0;                     // below the shoulder pad yoke
        float top = NeckZ - (front ? 27.0 : 25.0);
        float v = (top - z) / H;
        float w = H * 0.667, gap = H * 0.05;
        float n = round(Number);
        float tens = floor(n / 10.0), ones = n - tens * 10.0;
        bool two = n >= 10.0;
        float total = two ? (2.0 * w + gap) : w;
        float lx = (front ? P.x : -P.x) * NumFlip + total * 0.5;     // viewer's left to right (checked in -TagLineup)
        if (v > 0.0 && v < 1.0 && lx > 0.0 && lx < total)
        {
            float digit = -1.0, du = 0.0;
            if (!two) { digit = ones; du = lx / w; }
            else if (lx < w) { digit = tens; du = lx / w; }
            else if (lx > w + gap) { digit = ones; du = (lx - w - gap) / w; }
            if (digit >= 0.0)
            {
                float2 m = Texture2DSampleLevel(Numbers, NumbersSampler, float2((digit + du) / 10.0, v), 0).rg;
                c = lerp(c, Trim.rgb, m.g);
                c = lerp(c, NumColor.rgb, m.r);
            }
        }
    }
}
else if (ax > HandX) { c = Glove.rgb; cloth = 0.6; }
return float4(c, cloth);
"""

SCALARS = {"WaistZ": 110.0, "NeckZ": 160.0, "KneeZ": 48.0, "ShoeZ": 12.0, "SleeveX": 32.0,
           "LegX": 26.0, "ThighX": 10.0, "LegY": 0.0, "HandX": 62.0,
           "Number": 0.0, "TorsoY": 0.0, "NumFlip": 1.0}
RAW = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "RawArt")
TEX_DIR = "/Game/TAG/Textures"
FABRIC_HLSL = """float3 fab = Cloth > 0.99 ? MeshN : TwillN;
return lerp(SkinN, fab, saturate(Cloth));"""


def import_tex(name, normal=False):
    """TAG uniform texture pack (Tools/Art/gen_uniform_textures.py) from RawArt."""
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
    else:
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
        tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    tex.set_editor_property("srgb", False)
    EAL.save_loaded_asset(tex)
    return tex
VECTORS = {"NumColor": (1.0, 1.0, 1.0), "Jersey": (0.6, 0.05, 0.05), "Pants": (0.9, 0.9, 0.9), "Trim": (0.95, 0.75, 0.1),
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
    t_num = import_tex("T_TagNumbers")
    t_mesh = import_tex("T_TagJerseyMesh_N", normal=True)
    t_twill = import_tex("T_TagTwill_N", normal=True)
    names = ["P", "Skin", "Numbers"] + list(VECTORS.keys()) + list(SCALARS.keys())
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
    numtex = node(unreal.MaterialExpressionTextureObjectParameter)
    numtex.set_editor_property("parameter_name", "Numbers")
    numtex.set_editor_property("texture", t_num)
    numtex.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    MEL.connect_material_expressions(numtex, "", custom, "Numbers")
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
    def fabric(tex, pname, tile, yy):
        uv = MEL.create_material_expression(m, unreal.MaterialExpressionTextureCoordinate, -700, yy)
        uv.set_editor_property("u_tiling", tile)
        uv.set_editor_property("v_tiling", tile)
        t = MEL.create_material_expression(m, unreal.MaterialExpressionTextureSampleParameter2D, -500, yy)
        t.set_editor_property("parameter_name", pname)
        t.set_editor_property("texture", tex)
        t.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
        MEL.connect_material_expressions(uv, "", t, "UVs")
        return t

    meshn = fabric(t_mesh, "JerseyMeshN", 48.0, 440)
    twilln = fabric(t_twill, "TwillN", 64.0, 600)
    fab = MEL.create_material_expression(m, unreal.MaterialExpressionCustom, -250, 500)
    fab.set_editor_property("code", FABRIC_HLSL)
    fab.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    fab.set_editor_property("description", "TAG fabric normal")
    fins = []
    for n in ("Cloth", "MeshN", "TwillN", "SkinN"):
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        fins.append(ci)
    fab.set_editor_property("inputs", fins)
    MEL.connect_material_expressions(cloth, "", fab, "Cloth")
    MEL.connect_material_expressions(meshn, "RGB", fab, "MeshN")
    MEL.connect_material_expressions(twilln, "RGB", fab, "TwillN")
    MEL.connect_material_expressions(nm, "RGB", fab, "SkinN")
    MEL.connect_material_property(fab, "", unreal.MaterialProperty.MP_NORMAL)

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
