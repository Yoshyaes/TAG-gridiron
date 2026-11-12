# Unreal Editor Python (headless). Imports the TAG character meshes built from the personal model pack
# (Tools/Art/hero_jobs.py + hero_build.py, RawArt/Hero) into /Game/TAG/Hero, with their textures and materials.
#   UnrealEditor-Cmd.exe TAGGridiron.uproject -run=pythonscript -script="<this file>" -unattended -nosplash
# Env: TAG_HERO_RETEX=<regex> re-imports matching textures (a regenerated in-house map), TAG_HERO_ONLY=<regex> (mesh names), TAG_HERO_MATS=1 rebuilds the master materials, TAG_HERO_SKIP_MESH=1 only
# redoes materials + catalog. Report: Saved/hero_import.json. Catalog for the game: Content/TAG/Hero/hero_catalog.json.
#
# Two skeletons: SKM_TAG_HeroRig_Skeleton (players, coaches, officials; the pack's 268 bone hero rig with Unreal names)
# and SKM_TAG_CrowdRig_Skeleton (fans and sideline crew, 29 bones). The hero bootstrap mesh is the physics proxy
# (weights on the main bones only), so its generated physics asset is the shared ragdoll.
# Masters: M_TAG_Hero (opaque; colour modes plain / alpha tint / recolour / uniform + numbers) and M_TAG_HeroMasked
# (hair, lashes). Material instances are shared by identical parameter sets (MI_<kind>_<slot>_<hash>).
import hashlib
import json
import os
import re
import shutil
import traceback

import unreal

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
PROJ = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
SAVED = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir())
RAW = os.path.join(PROJ, "RawArt", "Hero")
ROOT = "/Game/TAG/Hero"
TEXDIR = ROOT + "/Textures"
MATDIR = ROOT + "/Materials"
PACK = "D:/MaddenRef/ModelPack/"
NUMBERS = "/Game/TAG/Textures/T_TagNumbers"
EYE = PACK + "Textures/characters/common/textures/eyes/eye_6_1_color.png"
SKIN = PACK + "Textures/characters/common/textures/skintones/"
FOLDER = {"Kit": "Kits", "Rig": "Rig", "Helmet": "Helmets", "Cap": "Helmets", "Head": "Heads", "Coach": "Coaches",
          "CoachHead": "Coaches", "CoachFull": "Coaches", "Ref": "Officials", "RefHead": "Officials", "Fan": "Crowd",
          "FanProp": "Crowd", "Crew": "Sideline", "Staff": "Sideline", "SidePlayer": "Sideline", "Story": "Story"}
BOOT = {"hero": "SKM_TAG_HeroRig", "crowd": "SKM_TAG_CrowdRig", "male": "SKM_TAG_MaleRig", "female": "SKM_TAG_FemaleRig",
        "child": "SKM_TAG_ChildRig", "sideplayer": "SKM_TAG_SidePlayerRig"}
REPORT = {"meshes": [], "errors": [], "textures": 0, "materials": 0}

COLOR_HLSL = r"""
float3 c = BC.rgb;
float lum = dot(BC.rgb, float3(0.3, 0.59, 0.11));
if (Mode < 0.5) c = BC.rgb * Tint.rgb;                                  // plain
else if (Mode < 1.5) c = lerp(BC.rgb, BC.rgb * Tint.rgb, BC.a);        // alpha marks the team coloured cloth (fans)
else if (Mode < 2.5)
{
    // recolour a baked colour by its shading; the flat neon patches are logo placeholders -> trim
    bool neon = BC.g > 0.7 && BC.r < 0.75 && BC.b < 0.3 && BC.g > BC.r + 0.25;
    c = neon ? Trim.rgb : lerp(BC.rgb, Tint.rgb * saturate(lum / max(LumRef, 0.02)), AlphaTint > 0.5 ? saturate(BC.a * 1.25) : 1.0);
}
else if (Mode > 3.5)
{
    // untextured sideline players (the pack ships them without a texture): a uniform painted by bind-pose region
    float ax = abs(P.x);
    float z = P.z;
    if (z > NeckZ + 2.0) c = Trim2.rgb;                       // helmet
    else if (ax > 58.0) c = float3(0.03, 0.03, 0.035);        // gloves
    else if (ax > 33.0) c = float3(0.30, 0.19, 0.13);         // forearms
    else if (z > 108.0) c = Tint.rgb;                         // jersey
    else if (z > 50.0) c = Trim.rgb;                          // pants
    else if (z > 14.0) c = Tint.rgb;                          // socks
    else c = float3(0.05, 0.05, 0.05);                        // cleats
}
else
{
    // uniform cloth: team colour with the stripe masks (R main trim, G second trim)
    c = lerp(lerp(Tint.rgb, Trim.rgb, M.r), Trim2.rgb, M.g) * saturate(lum / max(LumRef, 0.02));
}
if (LogoOn > 0.5 && abs(P.x) > LogoX)
{
    // the team mark on both sides of the shell (gen_team_logos.py atlas, 8 x 4 cells: R fill, G outline / badge),
    // projected across the side from the bind pose (front = +Y); upright and read left to right from either side
    float s = P.x < 0.0 ? 1.0 : -1.0;
    float u = s * (P.y - LogoY) / LogoW + 0.5;
    float v = (LogoZ - P.z) / LogoH + 0.5;
    if (u > 0.0 && u < 1.0 && v > 0.0 && v < 1.0)
    {
        float2 cell = float2(fmod(LogoIdx, 8.0), floor(LogoIdx / 8.0));
        float2 m = Texture2DSampleLevel(Logos, LogosSampler, (cell + float2(u, v)) / float2(8.0, 4.0), 0).rg;
        c = lerp(c, LogoLine.rgb, m.g);
        c = lerp(c, LogoFill.rgb, m.r);
    }
}
if (NumOn > 0.5)
{
    // numbers on chest and back, placed in the bind pose (front = +Y): TAG digit atlas, R fill, G outline
    float ax = abs(P.x);
    float side = P.y - TorsoY;
    if (abs(side) > 4.0 && ax < 16.0)
    {
        bool front = side > 0.0;
        float H = front ? NumHF : NumHB;
        float top = NeckZ - (front ? NumTopF : NumTopB);
        float v = (top - P.z) / H;
        float w = H * 0.667, gap = H * 0.05;
        float n = round(Number);
        float tens = floor(n / 10.0), ones = n - tens * 10.0;
        bool two = n >= 10.0;
        float total = two ? (2.0 * w + gap) : w;
        float lx = (front ? P.x : -P.x) * NumFlip + total * 0.5;
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
return c;
"""
SCALARS = {"Mode": 0.0, "AlphaTint": 1.0, "LumRef": 1.0, "NumOn": 0.0, "Number": 0.0, "NeckZ": 157.0, "TorsoY": 3.0, "NumFlip": 1.0,
           "NumHF": 19.0, "NumTopF": 14.0, "NumHB": 25.0, "NumTopB": 12.0,
           # helmet side mark (Shell): centre a touch behind the ear line, 11 x 10 cm, on the side past |x| 7 cm
           "LogoOn": 0.0, "LogoIdx": 0.0, "LogoX": 7.0, "LogoY": -1.0, "LogoZ": 174.5, "LogoW": 11.0, "LogoH": 10.0}
VECTORS = {"Tint": (1, 1, 1), "Trim": (0.8, 0.1, 0.1), "Trim2": (0.05, 0.05, 0.05), "NumColor": (1, 1, 1),
           "LogoFill": (1, 1, 1), "LogoLine": (0.05, 0.05, 0.05)}


def ensure_dir(p):
    if not EAL.does_directory_exist(p):
        EAL.make_directory(p)


# ------------------------------------------------------------------ textures
TEX_CACHE = {}


def tex_name(src):
    base = re.sub(r"[^A-Za-z0-9_]", "_", os.path.splitext(os.path.basename(src))[0])
    return "T_" + base[:60] + "_" + hashlib.md5(src.lower().replace("\\", "/").encode()).hexdigest()[:5]


def texture(src, kind="color"):
    """Import a PNG once (kind: color sRGB, linear, normal (OpenGL -> green flipped), mask)."""
    if not src:
        return None
    src = src.replace("\\", "/")
    key = (src, kind)
    if key in TEX_CACHE:
        return TEX_CACHE[key]
    if not os.path.exists(src):
        REPORT["errors"].append("missing texture " + src)
        TEX_CACHE[key] = None
        return None
    sub = "Heads" if "/players/players/" in src or "/coach/coaches/" in src else "Shared"
    folder = TEXDIR + "/" + sub
    name = tex_name(src) + ("_N" if kind == "normal" else "_L" if kind in ("linear", "mask") else "")
    path = folder + "/" + name
    retex = os.environ.get("TAG_HERO_RETEX")  # regex: re-import these textures over their assets (a regenerated map)
    if not EAL.does_asset_exist(path) or (retex and re.search(retex, name)):
        task = unreal.AssetImportTask()
        task.filename = src
        task.destination_path = folder
        task.destination_name = name
        task.replace_existing = True
        task.automated = True
        task.save = False
        tools.import_asset_tasks([task])
        t = unreal.load_asset(path)
        if t is None:
            REPORT["errors"].append("texture import failed " + src)
            TEX_CACHE[key] = None
            return None
        if kind == "normal":
            t.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
            t.set_editor_property("srgb", False)
            t.set_editor_property("flip_green_channel", True)
            t.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_CHARACTER_NORMAL_MAP)
        elif kind in ("linear", "mask"):
            t.set_editor_property("srgb", False)
            t.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_CHARACTER_SPECULAR)
        else:
            t.set_editor_property("srgb", True)
            t.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_CHARACTER)
        t.set_editor_property("max_texture_size", 2048)
        EAL.save_loaded_asset(t, only_if_is_dirty=False)
        REPORT["textures"] += 1
    t = unreal.load_asset(path)
    TEX_CACHE[key] = t
    return t


def raw_tex(name, kind="mask"):
    return texture(os.path.join(RAW, "Textures", name + ".png"), kind)


# ------------------------------------------------------------------ masters
def master(name, masked):
    path = MATDIR + "/" + name
    if EAL.does_asset_exist(path) and os.environ.get("TAG_HERO_MATS") != "1":
        return unreal.load_asset(path)
    ensure_dir(MATDIR)
    if EAL.does_asset_exist(path):
        m = unreal.load_asset(path)
        MEL.delete_all_material_expressions(m)
    else:
        m = tools.create_asset(name, MATDIR, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("used_with_skeletal_mesh", True)
    if masked:
        m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
        m.set_editor_property("two_sided", True)
        m.set_editor_property("opacity_mask_clip_value", 0.4)
    y = [0]

    def node(cls, x=-1300):
        e = MEL.create_material_expression(m, cls, x, y[0])
        y[0] += 150
        return e

    def scalar(n, v, x=-1300):
        e = node(unreal.MaterialExpressionScalarParameter, x)
        e.set_editor_property("parameter_name", n)
        e.set_editor_property("default_value", v)
        return e

    def vector(n, v, x=-1300):
        e = node(unreal.MaterialExpressionVectorParameter, x)
        e.set_editor_property("parameter_name", n)
        e.set_editor_property("default_value", unreal.LinearColor(v[0], v[1], v[2], 1))
        return e

    def custom(code, out, ins, desc, x=-700):
        c = MEL.create_material_expression(m, unreal.MaterialExpressionCustom, x, y[0])
        y[0] += 200
        c.set_editor_property("code", code)
        c.set_editor_property("output_type", out)
        c.set_editor_property("description", desc)
        lst = []
        for n in ins:
            ci = unreal.CustomInput()
            ci.set_editor_property("input_name", n)
            lst.append(ci)
        c.set_editor_property("inputs", lst)
        return c

    # crowd atlases: (uv + cell) / cells picks one of the N x N people in the texture
    uv = node(unreal.MaterialExpressionTextureCoordinate)
    cell = custom("return (UV + float2(CellU, CellV)) / max(Cells, 1.0);", unreal.CustomMaterialOutputType.CMOT_FLOAT2,
                  ["UV", "CellU", "CellV", "Cells"], "atlas cell")
    MEL.connect_material_expressions(uv, "", cell, "UV")
    for n, v in (("CellU", 0.0), ("CellV", 0.0), ("Cells", 1.0)):
        MEL.connect_material_expressions(scalar(n, v), "", cell, n)

    def tex(n, default, stype):
        e = node(unreal.MaterialExpressionTextureSampleParameter2D, -1000)
        e.set_editor_property("parameter_name", n)
        e.set_editor_property("texture", default)
        e.set_editor_property("sampler_type", stype)
        MEL.connect_material_expressions(cell, "", e, "UVs")
        return e

    bc = tex("BaseColor", raw_tex("T_TAG_Hero_White", "color"), unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    if masked:
        tint = vector("Tint", (1, 1, 1))
        mul = MEL.create_material_expression(m, unreal.MaterialExpressionMultiply, -500, 0)
        MEL.connect_material_expressions(bc, "RGB", mul, "A")
        MEL.connect_material_expressions(tint, "", mul, "B")
        MEL.connect_material_property(mul, "", unreal.MaterialProperty.MP_BASE_COLOR)
        MEL.connect_material_property(bc, "A", unreal.MaterialProperty.MP_OPACITY_MASK)
        MEL.connect_material_property(scalar("Roughness", 0.55), "", unreal.MaterialProperty.MP_ROUGHNESS)
        MEL.connect_material_property(scalar("Specular", 0.35), "", unreal.MaterialProperty.MP_SPECULAR)
    else:
        mask = tex("Mask", raw_tex("T_TAG_Hero_Black"), unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        nrm = tex("Normal", texture(os.path.join(RAW, "Textures", "T_TAG_Hero_FlatN.png"), "normal"), unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
        rsm = tex("RSM", raw_tex("T_TAG_Hero_RSM", "linear"), unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        pos = node(unreal.MaterialExpressionPreSkinnedPosition)
        vi = node(unreal.MaterialExpressionVertexInterpolator)
        MEL.connect_material_expressions(pos, "", vi, "")
        col = custom(COLOR_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                     ["BC", "M", "P", "Numbers", "Logos"] + list(VECTORS) + list(SCALARS), "TAG hero colour", -450)
        MEL.connect_material_expressions(bc, "RGBA", col, "BC")
        MEL.connect_material_expressions(mask, "RGB", col, "M")
        MEL.connect_material_expressions(vi, "", col, "P")
        num = node(unreal.MaterialExpressionTextureObjectParameter)
        num.set_editor_property("parameter_name", "Numbers")
        num.set_editor_property("texture", unreal.load_asset(NUMBERS))
        num.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        MEL.connect_material_expressions(num, "", col, "Numbers")
        logo = node(unreal.MaterialExpressionTextureObjectParameter)
        logo.set_editor_property("parameter_name", "Logos")
        logo.set_editor_property("texture", raw_tex("T_TAG_TeamLogos", "linear"))
        logo.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        MEL.connect_material_expressions(logo, "", col, "Logos")
        for k, v in VECTORS.items():
            MEL.connect_material_expressions(vector(k, v), "", col, k)
        for k, v in SCALARS.items():
            MEL.connect_material_expressions(scalar(k, v), "", col, k)
        MEL.connect_material_property(col, "", unreal.MaterialProperty.MP_BASE_COLOR)
        MEL.connect_material_property(nrm, "RGB", unreal.MaterialProperty.MP_NORMAL)
        # RSM: R roughness, G specular, B metallic (skin maps carry subsurface in B: MetalWeight 0)
        surf = custom("float r = lerp(RSM.r, RoughOver, RoughOverA);\nreturn float3(r, lerp(RSM.g, 0.5, SpecFlat), saturate(RSM.b * MetalWeight + MetalAdd));",
                      unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["RSM", "RoughOver", "RoughOverA", "SpecFlat", "MetalWeight", "MetalAdd"],
                      "TAG hero surface", -450)
        MEL.connect_material_expressions(rsm, "RGB", surf, "RSM")
        for n, v in (("RoughOver", 0.5), ("RoughOverA", 0.0), ("SpecFlat", 0.0), ("MetalWeight", 1.0), ("MetalAdd", 0.0)):
            MEL.connect_material_expressions(scalar(n, v), "", surf, n)
        for ch, prop in (("r", unreal.MaterialProperty.MP_ROUGHNESS), ("g", unreal.MaterialProperty.MP_SPECULAR),
                         ("b", unreal.MaterialProperty.MP_METALLIC)):
            cm = MEL.create_material_expression(m, unreal.MaterialExpressionComponentMask, -200, y[0])
            y[0] += 100
            for c in "rgba":
                cm.set_editor_property(c, c == ch)
            MEL.connect_material_expressions(surf, "", cm, "")
            MEL.connect_material_property(cm, "", prop)
    MEL.recompile_material(m)
    EAL.save_loaded_asset(m, only_if_is_dirty=False)
    return m


MI_CACHE = {}


def instance(kind, slot, masked, params):
    """Material instance for a parameter set, shared between meshes with the same one."""
    sig = json.dumps([masked, sorted((k, (v.get_path_name() if isinstance(v, unreal.Object) else v)) for k, v in params.items())])
    h = hashlib.md5(sig.encode()).hexdigest()[:8]
    name = "MI_%s_%s_%s" % (kind, slot, h)
    path = MATDIR + "/Instances/" + name
    if path in MI_CACHE:
        return MI_CACHE[path]
    if EAL.does_asset_exist(path):
        mi = unreal.load_asset(path)
    else:
        ensure_dir(MATDIR + "/Instances")
        mi = tools.create_asset(name, MATDIR + "/Instances", unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        REPORT["materials"] += 1
    MEL.set_material_instance_parent(mi, MASKED if masked else OPAQUE)
    for k, v in params.items():
        if v is None:
            continue
        if isinstance(v, unreal.Texture):
            MEL.set_material_instance_texture_parameter_value(mi, k, v)
        elif isinstance(v, (tuple, list)):
            MEL.set_material_instance_vector_parameter_value(mi, k, unreal.LinearColor(v[0], v[1], v[2], 1))
        else:
            MEL.set_material_instance_scalar_parameter_value(mi, k, float(v))
    EAL.save_loaded_asset(mi, only_if_is_dirty=False)
    MI_CACHE[path] = mi
    return mi


def first(texs, *keys):
    for t in texs.values():
        for k in keys:
            if t.get(k):
                return t[k]
    return None


def slot_params(entry, slot, info):
    """(masked, params) for one material slot, by the mesh kind and the slot's role (Top_2 -> Top)."""
    import re as _re
    kind = entry["kind"]
    role = _re.sub(r"_\d+$", "", slot)
    t = info.get("textures", {})
    col = t.get("colorTexture") or t.get("swarmColor")
    nrm = t.get("normalClampTexture")
    rsm = t.get("RSMTexture")
    skin_rsm = t.get("R_S_SSR_T_Texture")
    lum = float(t.get("_lum", 1.0) or 1.0)
    placeholder = bool(t.get("_placeholder"))
    if t.get("_placeholder") == "rainbow" and "_legs_" in (t.get("colorTexture") or ""):
        # bare legs whose texture the pack left as the placeholder: the character's own arm skin
        arm = (t.get("colorTexture") or "").replace("_legs_", "_arms_")
        if os.path.exists(arm):
            return False, {"BaseColor": texture(arm), "MetalWeight": 0.0, "SpecFlat": 0.3}
    if t.get("_placeholder") == "rainbow" and role not in ("Head", "Eyes", "Hair", "HairCap", "Lashes", "Skin"):
        # the game's missing-texture strip: plain cloth, painted in a team colour where the game paints Mode 2
        return False, {"Mode": 2.0, "LumRef": 1.0, "Tint": (0.42, 0.42, 0.45), "RoughOver": 0.8, "RoughOverA": 1.0, "MetalWeight": 0.0}
    flat = (lambda q: q is None or "flatgrey" in q or "invisible" in q)
    team_cloth = kind in ("Coach", "CoachFull", "Staff", "Story", "Crew")
    P = {}
    masked = False
    if role in ("Hair", "HairCap", "Lashes"):
        masked = True
        P = {"BaseColor": texture(col)}
    elif role == "Head":
        P = {"BaseColor": texture(col), "Normal": texture(nrm, "normal"), "RSM": texture(skin_rsm or rsm, "linear"),
             "MetalWeight": 0.0, "SpecFlat": 0.3}
    elif role == "Eyes":
        P = {"BaseColor": texture(col if col and not flat(col) else EYE), "RoughOver": 0.08, "RoughOverA": 1.0, "MetalWeight": 0.0}
    elif role == "Skin":
        if kind in ("Kit", "Rig"):
            P = {"BaseColor": texture(SKIN + "arm_skintone1.png"), "Normal": texture(SKIN + "arm_normal.png", "normal"),
                 "RSM": texture(SKIN + "arm_rsm_skintone2.png", "linear"), "MetalWeight": 0.0, "SpecFlat": 0.3}
        else:
            P = {"BaseColor": texture(col), "Normal": texture(nrm, "normal"), "RSM": texture(skin_rsm or rsm, "linear"),
                 "MetalWeight": 0.0, "SpecFlat": 0.3}
    elif role == "SkinLegs":
        P = {"BaseColor": texture(SKIN + "genericpadless_legs_st1.png"), "Normal": texture(nrm, "normal"),
             "RSM": texture(skin_rsm, "linear"), "MetalWeight": 0.0, "SpecFlat": 0.3}
    elif role == "Jersey":
        # in-house cloth (gen_kit_cloth_maps.py): mesh knit, roughness ~0.8 (a team's licensed jersey maps read as plastic)
        P = {"Mode": 3.0, "NumOn": 1.0, "Mask": raw_tex("T_TAG_Hero_JerseyMask"), "Normal": raw_tex("T_TAG_Hero_JerseyKnitN", "normal"),
             "RSM": raw_tex("T_TAG_Hero_JerseyRSM", "linear"), "MetalWeight": 0.0}
    elif role == "Pants" and kind in ("Kit", "Rig"):
        # in-house tackle twill, a dull sheen (roughness ~0.66)
        P = {"Mode": 3.0, "Mask": raw_tex("T_TAG_Hero_PantsMask"), "Normal": raw_tex("T_TAG_Hero_PantsTwillN", "normal"),
             "RSM": raw_tex("T_TAG_Hero_PantsRSM", "linear"), "MetalWeight": 0.0}
    elif role == "Socks":
        P = {"Mode": 3.0, "BaseColor": texture(col), "LumRef": 0.85, "Normal": texture(nrm, "normal"), "RSM": texture(rsm, "linear"), "MetalWeight": 0.0}
    elif role == "Shell":
        P = {"Mode": 3.0, "Mask": raw_tex("T_TAG_Hero_HelmetMask"), "Normal": texture(nrm, "normal"), "RoughOver": 0.16,
             "RoughOverA": 1.0, "MetalWeight": 0.0, "MetalAdd": 0.15}
    elif role == "Facemask":
        P = {"Tint": (0.2, 0.2, 0.21), "RoughOver": 0.32, "RoughOverA": 1.0, "MetalAdd": 0.55, "MetalWeight": 0.0}
    elif role == "Visor":
        P = {"Tint": (0.03, 0.03, 0.035), "RoughOver": 0.05, "RoughOverA": 1.0, "MetalAdd": 0.3, "MetalWeight": 0.0}
    elif role == "Hands" and flat(col):
        P = {"Tint": (0.04, 0.04, 0.045), "Normal": texture(nrm, "normal") if not flat(nrm) else None, "RoughOver": 0.6, "RoughOverA": 1.0, "MetalWeight": 0.0}
    elif role in ("Top", "Bottom", "Hat") and team_cloth and (placeholder or (kind == "Coach" and role == "Top")) and col:
        # team-colour placeholders (flat neon, the coaches' pure red / blue): painted per team in the game
        P = {"Mode": 2.0, "AlphaTint": 1.0 if role == "Top" else 0.0, "BaseColor": texture(col), "LumRef": lum, "Normal": texture(nrm, "normal") if not flat(nrm) else None,
             "RSM": texture(rsm, "linear") if not flat(rsm) else None, "MetalWeight": 0.0}
    elif kind == "SidePlayer":
        P = {"Mode": 4.0, "NeckZ": 158.0, "RoughOver": 0.6, "RoughOverA": 1.0, "MetalWeight": 0.0}
    elif role == "Body" and kind in ("Fan", "FanProp"):
        P = {"Mode": 1.0, "BaseColor": texture(col), "Cells": float(entry.get("meta", {}).get("cells", 1)), "MetalWeight": 0.0,
             "RoughOver": 0.75, "RoughOverA": 1.0}
    else:
        # gear and clothing with its own textures (cleats, tape, pads, coach, official, story, crew clothing)
        P = {"BaseColor": texture(col) if not flat(col) else None, "Normal": texture(nrm, "normal") if not flat(nrm) else None,
             "RSM": texture(rsm, "linear") if not flat(rsm) else None, "MetalWeight": 1.0 if rsm and not flat(rsm) else 0.0}
        if flat(col):
            P["Tint"] = (0.5, 0.5, 0.5)
    return masked, {k: v for k, v in P.items() if v is not None}


# ------------------------------------------------------------------ meshes
def pipeline(skeleton, physics):
    name = "P_TAG_Hero_" + ("Phys" if physics else "Mesh")
    pipe_path = ROOT + "/_Pipelines/" + name
    pipe = unreal.load_asset(pipe_path) if EAL.does_asset_exist(pipe_path) else None
    if pipe is None:
        base = unreal.load_asset("/Interchange/Pipelines/DefaultAssetsPipeline")
        pipe = tools.duplicate_asset(name, ROOT + "/_Pipelines", base)
    mp = pipe.get_editor_property("mesh_pipeline")
    mp.set_editor_property("import_static_meshes", False)
    mp.set_editor_property("import_skeletal_meshes", True)
    mp.set_editor_property("create_physics_asset", bool(physics))
    try:
        mp.set_editor_property("import_morph_targets", False)
    except Exception:
        pass
    common = pipe.get_editor_property("common_skeletal_meshes_and_animations_properties")
    common.set_editor_property("skeleton", skeleton)
    common.set_editor_property("import_only_animations", False)
    pipe.get_editor_property("animation_pipeline").set_editor_property("import_animations", False)
    pipe.get_editor_property("material_pipeline").set_editor_property("import_materials", False)
    try:
        pipe.get_editor_property("material_pipeline").set_editor_property("material_import", unreal.InterchangeMaterialImportOption.DO_NOT_CREATE_MATERIAL)
    except Exception:
        pass
    EAL.save_loaded_asset(pipe, only_if_is_dirty=False)
    return pipe_path + "." + name


def import_mesh(entry, skeleton, physics=False):
    dest = ROOT + "/" + FOLDER.get(entry["kind"], "Misc")
    ensure_dir(dest)
    tmp_dir = os.path.join(SAVED, "HeroImport")
    os.makedirs(tmp_dir, exist_ok=True)
    tmp = os.path.join(tmp_dir, entry["name"] + ".fbx")
    shutil.copyfile(entry["fbx"], tmp)
    params = unreal.ImportAssetParameters()
    params.is_automated = True
    params.replace_existing = True
    params.override_pipelines = [unreal.SoftObjectPath(pipeline(skeleton, physics))]
    src = unreal.InterchangeManager.create_source_data(tmp)
    mgr = unreal.InterchangeManager.get_interchange_manager_scripted()
    if not mgr.import_asset(dest, src, params):
        raise RuntimeError("interchange import failed " + entry["name"])
    path = dest + "/" + entry["name"]
    mesh = unreal.load_asset(path)
    if not isinstance(mesh, unreal.SkeletalMesh):
        raise RuntimeError("no skeletal mesh at " + path)
    try:
        os.remove(tmp)
    except OSError:
        pass
    return mesh


def apply_materials(entry, mesh):
    mats = mesh.get_editor_property("materials")
    slots = entry.get("slots", {})
    names = []
    for i in range(len(mats)):
        sm = mats[i]  # a copy of the struct: written back below
        slot = str(sm.get_editor_property("material_slot_name"))
        info = slots.get(slot)
        if info is None:  # Interchange may suffix slot names
            info = next((v for k, v in slots.items() if slot.startswith(k)), {"textures": {}})
            base = next((k for k in slots if slot.startswith(k)), slot)
        else:
            base = slot
        masked, P = slot_params(entry, base, info)
        sm.set_editor_property("material_interface", instance(entry["kind"], base, masked, P))
        mats[i] = sm
        names.append(base)
    mesh.set_editor_property("materials", mats)
    got = [m.get_editor_property("material_interface") for m in mesh.get_editor_property("materials")]
    if any(g is None or "WorldGrid" in g.get_path_name() for g in got):
        raise RuntimeError("material slots did not take the instances: %s" % [g.get_path_name() if g else None for g in got])
    return names


def main():
    global OPAQUE, MASKED
    unreal.AssetRegistryHelpers.get_asset_registry().search_all_assets(True)
    index = json.load(open(os.path.join(RAW, "hero_index.json")))
    OPAQUE = master("M_TAG_Hero", False)
    MASKED = master("M_TAG_HeroMasked", True)
    only = os.environ.get("TAG_HERO_ONLY")
    rx = re.compile(only, re.I) if only else None
    skip_mesh = os.environ.get("TAG_HERO_SKIP_MESH") == "1"
    skel = {}
    for rig in sorted({x.get("rig", "hero") for x in index}):
        boot = BOOT[rig]
        mesh_path = ROOT + "/Rig/" + boot
        if not EAL.does_asset_exist(mesh_path):
            e = next(x for x in index if x["name"] == boot)
            import_mesh(e, None, physics=(rig == "hero"))
            unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
        # the skeleton Interchange made for the bootstrap mesh (named after the mesh or after its root bone)
        bm = unreal.load_asset(mesh_path)
        skel[rig] = bm.get_editor_property("skeleton") if bm else None
        path = skel[rig].get_path_name() if skel[rig] else mesh_path + "_Skeleton"
        REPORT.setdefault("skeletons", {})[rig] = path
        if skel[rig] is None:
            raise RuntimeError("skeleton missing after bootstrap " + path)
    phys = unreal.load_asset(ROOT + "/Rig/SKM_TAG_HeroRig_PhysicsAsset")
    for e in index:
        if e["kind"] == "Rig" or (rx and not rx.search(e["name"])):
            continue
        r = {"name": e["name"], "kind": e["kind"]}
        try:
            path = ROOT + "/" + FOLDER.get(e["kind"], "Misc") + "/" + e["name"]
            if os.environ.get("TAG_HERO_SKIP_EXISTING") == "1" and EAL.does_asset_exist(path):
                r["status"] = "ok"
                r["path"] = path
                r["skipped"] = True
                REPORT["meshes"].append(r)
                continue
            if skip_mesh and EAL.does_asset_exist(path):
                mesh = unreal.load_asset(path)
            else:
                mesh = import_mesh(e, skel[e.get("rig", "hero")])
            if e["kind"] in ("Kit", "Coach", "CoachFull", "Ref", "Staff") and phys and e.get("rig", "hero") == "hero":
                mesh.set_editor_property("physics_asset", phys)
            r["slots"] = apply_materials(e, mesh)
            if not EAL.save_asset(path, only_if_is_dirty=False):
                raise RuntimeError("save failed (editor open?) " + path)
            r["path"] = path
            r["skeleton"] = mesh.get_editor_property("skeleton").get_path_name()
            r["bones"] = mesh.get_editor_property("skeleton") and len(unreal.AnimPoseExtensions.get_bone_names(unreal.AnimPoseExtensions.get_reference_pose(mesh.get_editor_property("skeleton"))))
            b = mesh.get_bounds()
            r["bbox_cm"] = [round(b.box_extent.x * 2, 1), round(b.box_extent.y * 2, 1), round(b.box_extent.z * 2, 1)]
            r["status"] = "ok"
        except Exception as ex:
            r["status"] = "fail"
            r["error"] = repr(ex)
            r["trace"] = traceback.format_exc()[-1200:]
        REPORT["meshes"].append(r)
        unreal.log("TAG_HERO %s %s" % (r["status"], e["name"]))
    # catalog for the game (every mesh that exists, not only this run's)
    cat = {"skeleton": ROOT + "/Rig/SKM_TAG_HeroRig_Skeleton", "crowd_skeleton": ROOT + "/Rig/SKM_TAG_CrowdRig_Skeleton",
           "skeletons": REPORT.get("skeletons", {}),
           "physics": ROOT + "/Rig/SKM_TAG_HeroRig_PhysicsAsset", "proxy": ROOT + "/Rig/SKM_TAG_HeroRig",
           "skin_arm": [], "skin_legs": [], "eyes": [], "items": []}
    # stripe layouts from every uniform set in the pack (Tools/Art/hero_masks.py); the game gives each team one
    import glob as _g
    cat["masks"] = {}
    for part in ("Jersey", "Pants", "Helmet"):
        names = sorted(os.path.basename(f)[:-4] for f in _g.glob(os.path.join(RAW, "Textures", "T_TAG_Hero_%sMask*.png" % part)))
        cat["masks"][part] = [t.get_path_name() for t in (raw_tex(n) for n in names) if t]
    for n in range(1, 9):
        a = texture(SKIN + "arm_skintone%d.png" % n)
        l = texture(SKIN + "genericpadless_legs_st%d.png" % n)
        cat["skin_arm"].append(a.get_path_name() if a else "")
        cat["skin_legs"].append(l.get_path_name() if l else "")
    for f in sorted(os.listdir(PACK + "Textures/characters/common/textures/eyes")):
        if f.endswith("_color.png"):
            t = texture(PACK + "Textures/characters/common/textures/eyes/" + f)
            if t:
                cat["eyes"].append(t.get_path_name())
    for e in index:
        path = ROOT + "/" + FOLDER.get(e["kind"], "Misc") + "/" + e["name"]
        if e["kind"] == "Rig" or not EAL.does_asset_exist(path):
            continue
        it = {"kind": e["kind"], "name": e["name"], "rig": e.get("rig", "hero"), "path": path + "." + e["name"]}
        b = e.get("bounds_cm")
        if b:
            it["height"] = round(b[1][2] - b[0][2], 1)
        if "far" in e:
            it["far"] = e["far"]
        it.update({k: v for k, v in e.get("meta", {}).items() if isinstance(v, (int, float, str, bool))})
        cat["items"].append(it)
    out = os.path.join(PROJ, "Content", "TAG", "Hero", "hero_catalog.json")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    json.dump(cat, open(out, "w"), indent=1)
    REPORT["catalog_items"] = len(cat["items"])


try:
    main()
except Exception:
    REPORT["errors"].append(traceback.format_exc())
with open(os.environ.get("TAG_HERO_REPORT") or os.path.join(SAVED, "hero_import.json"), "w") as fh:
    json.dump(REPORT, fh, indent=1)
unreal.log("TAG_HERO done meshes=%d errors=%d" % (len(REPORT["meshes"]), len(REPORT["errors"])))
