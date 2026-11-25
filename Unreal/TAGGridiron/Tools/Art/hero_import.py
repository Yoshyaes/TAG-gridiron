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
# channel layout of the pack's colour textures (Tools/Art/hero_tex_layout.py): "R" / "RB" / "RG" are packed gear maps
_LAYOUT_FILE = os.path.join(RAW, "tex_layout.json")
LAYOUT = json.load(open(_LAYOUT_FILE)) if os.path.exists(_LAYOUT_FILE) else {}

# pack characters the game cannot show as the pack has them (found in the Story gallery, the same on both animation
# trees): imported, but left out of the catalog
UNUSABLE = {}
# (repaired in the build instead: Reporter1's torn top is re-skinned, hero_full.py reskin_far; Watson's sleeveless
# athletic outfit takes the pack's shared full arm under his own skin texture; CoachTexans, whose every texture is the
# missing-texture rainbow, gets a donor face, skin in its tone, a close crop and a team-painted outfit)

COLOR_HLSL = r"""
// Packed: one of the pack's gear maps the extraction wrote into the red channel (Madden's single-channel BC4 maps),
// some with a region mask in blue (cleats: sole, heel, tongue, brand mark) or green. Madden shades them as a grey
// overlay, mid grey = the gear colour, the mask picking the second colour (Tools/Art/hero_tex_layout.py).
float4 B = BC;
float reg = 0.0;
if (Packed > 0.5)
{
    float g = pow(saturate(2.0 * pow(max(BC.r, 0.0), 1.0 / 2.2)), 2.2);
    reg = saturate(BC.g + BC.b);
    B = float4(g, g, g, BC.a);
}
float3 c = B.rgb;
float lum = dot(B.rgb, float3(0.3, 0.59, 0.11));
if (Mode < 0.5) c = B.rgb * lerp(Tint.rgb, Trim.rgb, reg);             // plain (packed: the mask takes the trim)
else if (Mode < 1.5) c = lerp(B.rgb, B.rgb * Tint.rgb, B.a);           // alpha marks the team coloured cloth (fans)
else if (Mode < 2.5)
{
    // recolour a baked colour by its shading; the flat neon patches are logo placeholders -> trim
    bool neon = Packed < 0.5 && BC.g > 0.7 && BC.r < 0.75 && BC.b < 0.3 && BC.g > BC.r + 0.25;
    c = neon ? Trim.rgb : lerp(B.rgb, lerp(Tint.rgb, Trim.rgb, reg) * saturate(lum / max(LumRef, 0.02)), AlphaTint > 0.5 && Packed < 0.5 ? saturate(BC.a * 1.25) : 1.0);
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
    // (see below for the officials' patch)
    // TV numbers: the same digits on top of each shoulder (NFL jerseys carry them on the shoulders or the sleeves so the
    // high cameras can read a man from above), projected down from the bind pose, their tops toward the back
    if (TvOn > 0.5 && P.z > NeckZ - TvZ)
    {
        float H = TvH;
        float w = H * 0.667, gap = H * 0.05;
        float n = round(Number);
        float tens = floor(n / 10.0), ones = n - tens * 10.0;
        bool two = n >= 10.0;
        float total = two ? (2.0 * w + gap) : w;
        float sx = P.x > 0.0 ? 1.0 : -1.0;
        float lx = (P.x - sx * TvX) * NumFlip + total * 0.5;
        float v = (P.y - TorsoY - TvY) / H + 0.5;
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
if (PatchOn > 0.5)
{
    // an official's back: his position letters over his number, the pack's own patch (a strip of eight cells, seven
    // used: DJ LJ R U BJ SJ FJ), placed in the bind pose like the players' numbers
    float side = P.y - TorsoY;
    if (side < -4.0)
    {
        float u = (-P.x * NumFlip) / PatchW + 0.5;
        float v = (NeckZ - PatchTop - P.z) / PatchH;
        if (u > 0.0 && u < 1.0 && v > 0.0 && v < 1.0)
        {
            float m = Texture2DSampleLevel(Patch, PatchSampler, float2((PatchIdx + u) / 8.0, v), 0).r;
            c = lerp(c, float3(0.02, 0.02, 0.025), m);
        }
    }
}
return c;
"""
SCALARS = {"Mode": 0.0, "Packed": 0.0, "TeamGlove": 0.0, "AlphaTint": 1.0, "LumRef": 1.0, "NumOn": 0.0, "Number": 0.0, "NeckZ": 157.0, "TorsoY": 3.0, "NumFlip": 1.0,
           "NumHF": 19.0, "NumTopF": 14.0, "NumHB": 25.0, "NumTopB": 12.0,
           # TV numbers on the shoulder tops: off (the game turns them on for the kits), digit height, centre |x|, y offset
           # from the torso divide, and how far under the neck the top surface still takes them
           "TvOn": 0.0, "TvH": 9.0, "TvX": 15.5, "TvY": 0.0, "TvZ": 11.0,
           # an official's back patch: off, which of the strip's cells, its top under the neck, its size (cm)
           "PatchOn": 0.0, "PatchIdx": 0.0, "PatchTop": 7.0, "PatchH": 22.0, "PatchW": 22.0,
           # helmet side mark (Shell): centre a touch behind the ear line, 11 x 10 cm, on the side past |x| 7 cm
           "LogoOn": 0.0, "LogoIdx": 0.0, "LogoX": 7.0, "LogoY": -1.0, "LogoZ": 174.5, "LogoW": 11.0, "LogoH": 10.0}
VECTORS = {"Tint": (1, 1, 1), "Trim": (0.8, 0.1, 0.1), "Trim2": (0.05, 0.05, 0.05), "NumColor": (1, 1, 1),
           "LogoFill": (1, 1, 1), "LogoLine": (0.05, 0.05, 0.05)}

# A man's kit through the game (Research/NextLevel/03 A6): Render/Stadium.cpp StepPlayers hands each man's meshes nine
# numbers as custom primitive data (no material instance is touched); a mesh nobody sets reads 0, so fans, crew and
# everyone before the game look as they did. Index: knees and shins, elbows and forearms, chest and front, back and
# seat, helmet (grime 0..1), wet, snow on top, sweat, mud (the stain's colour: 0 grass, 1 mud).
KIT_DATA = ["GrimeKnee", "GrimeArm", "GrimeFront", "GrimeBack", "GrimeHelmet", "KitWet", "KitSnow", "KitSweat", "KitMud",
            "HelmetPlain"]  # (9: Phase 4 A9, 1 = the shell without its clear coat and scuffs, -TagOldHelmet)
# where on him (the bind pose: z up from the soles, front = +Y, x across), broken up by a tiling noise; out: dirt,
# wetness (cloth soaks, skin and the shell only take a film), snow (on what faces up, never on skin), sweat
WEATHER_MASK_HLSL = r"""
float ax = abs(P.x);
float z = P.z;
float front = P.y > TorsoY ? 1.0 : 0.0;
float helm = z > NeckZ + 2.0 ? 1.0 : 0.0;
float body = 1.0 - helm;
float legs = saturate((62.0 - z) / 10.0) * saturate((z - 12.0) / 6.0);
float arms = saturate((ax - 26.0) / 8.0) * body;
float torso = saturate((z - 70.0) / 10.0) * saturate((NeckZ - 4.0 - z) / 6.0) * (1.0 - arms) * body;
float thighs = saturate((z - 55.0) / 8.0) * saturate((100.0 - z) / 8.0) * (1.0 - arms);
float g = GrimeKnee * legs * lerp(0.4, 1.0, front)
        + GrimeArm * arms
        + GrimeFront * max(torso, thighs * 0.8) * front
        + GrimeBack * max(torso, thighs) * (1.0 - front)
        + GrimeHelmet * helm;
g *= 1.0 - IsSkin * helm;  // (the face is skin: never dirtied)
float n1 = Texture2DSample(Noise, NoiseSampler, float2(P.x + P.y * 0.7, P.z) / 23.0).r;
float n2 = Texture2DSample(Noise, NoiseSampler, float2(P.y - P.x * 0.6, P.z * 1.7) / 9.0 + 0.37).g;
float n = n1 * 0.6 + n2 * 0.4;
float dirt = saturate((saturate(g) * 1.25 - (1.0 - n)) * 3.0);
float porous = lerp(1.0, 0.12, max(helm, IsSkin));
float wet = saturate(KitWet) * porous;
float upw = saturate((NW.z - 0.55) * 3.0);
float snow = saturate(KitSnow) * upw * (1.0 - IsSkin) * saturate(n * 1.6 - 0.2);
float sweat = saturate(KitSweat) * (IsSkin > 0.5 ? 1.0 : torso * 0.3);
return float4(dirt, wet, snow, sweat);
"""
# the colour: grass-green streaks or mud (by KitMud), soaked cloth darker (its pores fill with water), snow on top
WEATHER_COLOR_HLSL = r"""
float3 stain = lerp(float3(0.055, 0.075, 0.025), float3(0.085, 0.060, 0.038), saturate(KitMud));
float3 o = C;
o = lerp(o, stain, Mask.x * 0.85);
o *= lerp(1.0, 0.55, Mask.y);
o = lerp(o, float3(0.80, 0.82, 0.86), Mask.z);
return o;
"""
# roughness / specular / metal (the surface node's): soaked cloth a dull sheen, a film of water on skin and the shell,
# sweat on skin, dirt matte (and not metal), snow
WEATHER_SURF_HLSL = r"""
float helm = P.z > NeckZ + 2.0 ? 1.0 : 0.0;
float r = S.x;
r = lerp(r, min(r, 0.5), Mask.y * (1.0 - IsSkin));
r = lerp(r, min(r, 0.22), saturate(KitWet) * max(IsSkin, helm) * 0.7);
r = lerp(r, min(r, 0.3), Mask.w * IsSkin);
r = lerp(r, 0.92, Mask.x * 0.7);
r = lerp(r, 0.65, Mask.z);
return float3(saturate(r), lerp(S.y, 0.5, Mask.z), S.z * (1.0 - Mask.x * 0.8));
"""
GRIME_NOISE = "/Game/TAG/Textures/T_TagTurfMacro"  # tiling cloud noise (Tools/Art/ue_build_turf.py imports it)
# Phase 4 A9, the helmet shell (M_TAG_HeroCoat, clear coat): a hard lacquer over the team paint (coat roughness 0.05,
# 0.03 wet), the paint itself softer under it; as the game goes (the kit's helmet grime) scratches cut through the
# lacquer and scrape the paint back toward the white shell, the side marks with it. Out: coat, coat roughness, scuff.
COAT_HLSL = r"""
float helm = P.z > NeckZ + 2.0 ? 1.0 : 0.0;
float n = Texture2DSample(Noise, NoiseSampler, float2(P.x * 0.6 + P.y, P.z * 2.5) / 7.0).b;
float scuff = saturate((n - (1.0 - saturate(GrimeHelmet) * 0.85)) * 5.0) * helm * (1.0 - HelmetPlain);
float coat = (1.0 - HelmetPlain) * lerp(1.0, 0.35, scuff);
float cr = lerp(lerp(0.05, 0.03, saturate(KitWet)), 0.3, scuff);
return float3(coat, cr, scuff);
"""


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
SSS_PROFILE = MATDIR + "/SSP_TAG_Skin"


def skin_profile():
    """The subsurface profile the skin master uses (engine defaults are a human skin profile)."""
    if EAL.does_asset_exist(SSS_PROFILE):
        return unreal.load_asset(SSS_PROFILE)
    fac = getattr(unreal, "SubsurfaceProfileFactory", None)
    if fac is None:
        return None
    ensure_dir(MATDIR)
    prof = tools.create_asset("SSP_TAG_Skin", MATDIR, unreal.SubsurfaceProfile, fac())
    if prof:
        EAL.save_loaded_asset(prof, only_if_is_dirty=False)
    return prof


def master(name, masked, skin=False, coat=False):
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
    attrs = None
    if coat:
        # (the clear coat pins are hidden from Python: the coat master goes out through Make Material Attributes)
        m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_CLEAR_COAT)
        m.set_editor_property("use_material_attributes", True)
        attrs = MEL.create_material_expression(m, unreal.MaterialExpressionMakeMaterialAttributes, 300, 0)
        MEL.connect_material_property(attrs, "", unreal.MaterialProperty.MP_MATERIAL_ATTRIBUTES)

    def prop_out(src, out, prop, pin):
        """A material output: the property itself, or the coat master's attribute pin (Make Material Attributes)."""
        if attrs is None:
            MEL.connect_material_property(src, out, prop)
            return
        for name in (pin, re.sub(r"(?<!^)([A-Z])", r" ", pin)):
            if MEL.connect_material_expressions(src, out, attrs, name):
                return
        raise RuntimeError("%s: no attribute pin %s" % (name, pin))
    if skin:
        # the pack's skin maps carry subsurface (B) and translucency (A) beside roughness and specular: a scattering
        # skin, not Default Lit plastic (faces, arms, legs)
        prof = skin_profile()
        if prof:
            m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_SUBSURFACE_PROFILE)
            m.set_editor_property("subsurface_profile", prof)
        else:
            m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_SUBSURFACE)
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
        # strands: the pack's hair normal, and a dithered edge the temporal AA resolves into soft strands (a hard
        # 0.4 clip aliased into a cut-out helmet of hair)
        hn = tex("Normal", texture(os.path.join(RAW, "Textures", "T_TAG_Hero_FlatN.png"), "normal"), unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
        MEL.connect_material_property(hn, "RGB", unreal.MaterialProperty.MP_NORMAL)
        dfn = unreal.load_asset("/Engine/Functions/Engine_MaterialFunctions02/Utility/DitherTemporalAA")
        dith = MEL.create_material_expression(m, unreal.MaterialExpressionMaterialFunctionCall, -500, 300) if dfn else None
        if dith:
            dith.set_editor_property("material_function", dfn)
        if dith and MEL.connect_material_expressions(bc, "A", dith, "Alpha Threshold"):
            MEL.connect_material_property(dith, "", unreal.MaterialProperty.MP_OPACITY_MASK)
        else:
            MEL.connect_material_property(bc, "A", unreal.MaterialProperty.MP_OPACITY_MASK)
        MEL.connect_material_property(scalar("Roughness", 0.55), "", unreal.MaterialProperty.MP_ROUGHNESS)
        MEL.connect_material_property(scalar("Specular", 0.35), "", unreal.MaterialProperty.MP_SPECULAR)
    else:
        mask = tex("Mask", raw_tex("T_TAG_Hero_Black"), unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        nrm = tex("Normal", texture(os.path.join(RAW, "Textures", "T_TAG_Hero_FlatN.png"), "normal"), unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
        # the normal from the mesh's second UV set when NormalUV1 (helmet shells: the pack's vent / hardware detail
        # normal lies on its own set, kept as TexCoord 1 by hero_build.py)
        uv1 = node(unreal.MaterialExpressionTextureCoordinate)
        uv1.set_editor_property("coordinate_index", 1)
        nuv = custom("return NormalUV1 > 0.5 ? UV1 : UV0;", unreal.CustomMaterialOutputType.CMOT_FLOAT2, ["UV0", "UV1", "NormalUV1"], "normal uv")
        MEL.connect_material_expressions(cell, "", nuv, "UV0")
        MEL.connect_material_expressions(uv1, "", nuv, "UV1")
        MEL.connect_material_expressions(scalar("NormalUV1", 0.0), "", nuv, "NormalUV1")
        MEL.connect_material_expressions(nuv, "", nrm, "UVs")
        rsm = tex("RSM", raw_tex("T_TAG_Hero_RSM", "linear"), unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        pos = node(unreal.MaterialExpressionPreSkinnedPosition)
        vi = node(unreal.MaterialExpressionVertexInterpolator)
        MEL.connect_material_expressions(pos, "", vi, "")
        col = custom(COLOR_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                     ["BC", "M", "P", "Numbers", "Logos", "Patch"] + list(VECTORS) + list(SCALARS), "TAG hero colour", -450)
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
        patch = node(unreal.MaterialExpressionTextureObjectParameter)
        patch.set_editor_property("parameter_name", "Patch")
        patch.set_editor_property("texture", raw_tex("T_TAG_Hero_RefPatches", "linear"))
        patch.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        MEL.connect_material_expressions(patch, "", col, "Patch")
        for k, v in VECTORS.items():
            MEL.connect_material_expressions(vector(k, v), "", col, k)
        sp = {}
        for k, v in SCALARS.items():
            sp[k] = scalar(k, v)
            MEL.connect_material_expressions(sp[k], "", col, k)
        # the pack's decal layer (logoOverlayTexture: the cleats' brand marks), over the colour by its alpha
        # (an sRGB default: a linear one fails the Color sampler and the whole master falls back to the default material)
        dec = tex("Decal", raw_tex("T_TAG_Hero_White", "color"), unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
        over = custom("return lerp(C, D.rgb * DecalTint, saturate(D.a * DecalOn));", unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                      ["C", "D", "DecalTint", "DecalOn"], "TAG decal", -300)
        MEL.connect_material_expressions(col, "", over, "C")
        MEL.connect_material_expressions(dec, "RGBA", over, "D")
        MEL.connect_material_expressions(vector("DecalTint", (1, 1, 1)), "", over, "DecalTint")
        MEL.connect_material_expressions(scalar("DecalOn", 0.0), "", over, "DecalOn")
        # A6: the kit's weather through the game (KIT_DATA as custom primitive data, set by Render/Stadium.cpp)
        kit = {}
        for i, n in enumerate(KIT_DATA):
            e = scalar(n, 0.0)
            e.set_editor_property("use_custom_primitive_data", True)
            e.set_editor_property("primitive_data_index", i)
            kit[n] = e
        is_skin = node(unreal.MaterialExpressionConstant)
        is_skin.set_editor_property("r", 1.0 if skin else 0.0)
        have_noise = EAL.does_asset_exist(GRIME_NOISE)
        gnoise = node(unreal.MaterialExpressionTextureObjectParameter)
        gnoise.set_editor_property("parameter_name", "GrimeNoise")
        gnoise.set_editor_property("texture", unreal.load_asset(GRIME_NOISE) if have_noise else raw_tex("T_TAG_Hero_White", "color"))
        gnoise.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR if have_noise
                                   else unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
        wmask = custom(WEATHER_MASK_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT4,
                       ["P", "NW", "Noise", "IsSkin", "NeckZ", "TorsoY"] + KIT_DATA[:8], "TAG hero kit weather", -450)
        MEL.connect_material_expressions(vi, "", wmask, "P")
        MEL.connect_material_expressions(node(unreal.MaterialExpressionVertexNormalWS), "", wmask, "NW")
        MEL.connect_material_expressions(gnoise, "", wmask, "Noise")
        MEL.connect_material_expressions(is_skin, "", wmask, "IsSkin")
        MEL.connect_material_expressions(sp["NeckZ"], "", wmask, "NeckZ")
        MEL.connect_material_expressions(sp["TorsoY"], "", wmask, "TorsoY")
        for n in KIT_DATA[:8]:
            MEL.connect_material_expressions(kit[n], "", wmask, n)
        wcol = custom(WEATHER_COLOR_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["C", "Mask", "KitMud"], "TAG hero kit colour", -250)
        MEL.connect_material_expressions(over, "", wcol, "C")
        MEL.connect_material_expressions(wmask, "", wcol, "Mask")
        MEL.connect_material_expressions(kit["KitMud"], "", wcol, "KitMud")
        coat_node = None
        if coat:
            coat_node = custom(COAT_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                               ["P", "NeckZ", "GrimeHelmet", "KitWet", "HelmetPlain", "Noise"], "TAG helmet coat", -250)
            MEL.connect_material_expressions(vi, "", coat_node, "P")
            MEL.connect_material_expressions(sp["NeckZ"], "", coat_node, "NeckZ")
            for n in ("GrimeHelmet", "KitWet", "HelmetPlain"):
                MEL.connect_material_expressions(kit[n], "", coat_node, n)
            MEL.connect_material_expressions(gnoise, "", coat_node, "Noise")
            scr = custom("return lerp(C, max(C, float3(0.62, 0.62, 0.6)), CS.z * 0.7);", unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                         ["C", "CS"], "TAG helmet scrapes", -150)
            MEL.connect_material_expressions(wcol, "", scr, "C")
            MEL.connect_material_expressions(coat_node, "", scr, "CS")
            prop_out(scr, "", unreal.MaterialProperty.MP_BASE_COLOR, "BaseColor")
            for ch, pin in (("r", "ClearCoat"), ("g", "ClearCoatRoughness")):
                cm = MEL.create_material_expression(m, unreal.MaterialExpressionComponentMask, -100, y[0])
                y[0] += 100
                for c in "rgba":
                    cm.set_editor_property(c, c == ch)
                MEL.connect_material_expressions(coat_node, "", cm, "")
                prop_out(cm, "", None, pin)
        else:
            prop_out(wcol, "", unreal.MaterialProperty.MP_BASE_COLOR, "BaseColor")
        # a detail normal over the main one (the in-house knit / twill over the pack's jersey folds), reoriented
        # normal mapping; DetailOn 0 leaves the main normal alone
        det_uv = custom("return UV * DetailTile;", unreal.CustomMaterialOutputType.CMOT_FLOAT2, ["UV", "DetailTile"], "detail uv")
        MEL.connect_material_expressions(cell, "", det_uv, "UV")
        MEL.connect_material_expressions(scalar("DetailTile", 1.0), "", det_uv, "DetailTile")
        dn = node(unreal.MaterialExpressionTextureSampleParameter2D, -1000)
        dn.set_editor_property("parameter_name", "DetailN")
        dn.set_editor_property("texture", texture(os.path.join(RAW, "Textures", "T_TAG_Hero_FlatN.png"), "normal"))
        dn.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
        MEL.connect_material_expressions(det_uv, "", dn, "UVs")
        blend = custom("if (DetailOn < 0.5) return N;\nfloat3 t = N + float3(0, 0, 1);\nfloat3 u = D * float3(-1, -1, 1);\n"
                       "return normalize(t * dot(t, u) - u * t.z);", unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                       ["N", "D", "DetailOn"], "TAG detail normal", -450)
        MEL.connect_material_expressions(nrm, "RGB", blend, "N")
        MEL.connect_material_expressions(dn, "RGB", blend, "D")
        MEL.connect_material_expressions(scalar("DetailOn", 0.0), "", blend, "DetailOn")
        prop_out(blend, "", unreal.MaterialProperty.MP_NORMAL, "Normal")
        if skin:
            # the skin map's B: how much light scatters under the skin (thin ears and nose, not the scalp)
            sss = MEL.create_material_expression(m, unreal.MaterialExpressionComponentMask, -200, y[0] + 400)
            for c in "rgba":
                sss.set_editor_property(c, c == "b")
            MEL.connect_material_expressions(rsm, "RGB", sss, "")
            MEL.connect_material_property(sss, "", unreal.MaterialProperty.MP_OPACITY)
        # RSM: R roughness, G specular, B metallic (skin maps carry subsurface in B: MetalWeight 0)
        surf = custom("float r = lerp(RSM.r, RoughOver, RoughOverA);\nreturn float3(r, lerp(RSM.g, 0.5, SpecFlat), saturate(RSM.b * MetalWeight + MetalAdd));",
                      unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["RSM", "RoughOver", "RoughOverA", "SpecFlat", "MetalWeight", "MetalAdd"],
                      "TAG hero surface", -450)
        MEL.connect_material_expressions(rsm, "RGB", surf, "RSM")
        for n, v in (("RoughOver", 0.5), ("RoughOverA", 0.0), ("SpecFlat", 0.0), ("MetalWeight", 1.0), ("MetalAdd", 0.0)):
            MEL.connect_material_expressions(scalar(n, v), "", surf, n)
        # A6: the kit's weather on the surface (wet sheen, sweat, matte dirt, snow); all 0: the surface as it was
        wsurf = custom(WEATHER_SURF_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["S", "Mask", "P", "NeckZ", "IsSkin", "KitWet"],
                       "TAG hero kit surface", -300)
        MEL.connect_material_expressions(surf, "", wsurf, "S")
        MEL.connect_material_expressions(wmask, "", wsurf, "Mask")
        MEL.connect_material_expressions(vi, "", wsurf, "P")
        MEL.connect_material_expressions(sp["NeckZ"], "", wsurf, "NeckZ")
        MEL.connect_material_expressions(is_skin, "", wsurf, "IsSkin")
        MEL.connect_material_expressions(kit["KitWet"], "", wsurf, "KitWet")
        surf_out = wsurf
        if coat:
            soft = custom("return float3(lerp(max(S.x, 0.32), S.x, HelmetPlain), S.y, S.z);", unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                          ["S", "HelmetPlain"], "TAG helmet paint under the coat", -250)
            MEL.connect_material_expressions(wsurf, "", soft, "S")
            MEL.connect_material_expressions(kit["HelmetPlain"], "", soft, "HelmetPlain")
            surf_out = soft
        for ch, prop, pin in (("r", unreal.MaterialProperty.MP_ROUGHNESS, "Roughness"), ("g", unreal.MaterialProperty.MP_SPECULAR, "Specular"),
                              ("b", unreal.MaterialProperty.MP_METALLIC, "Metallic")):
            cm = MEL.create_material_expression(m, unreal.MaterialExpressionComponentMask, -200, y[0])
            y[0] += 100
            for c in "rgba":
                cm.set_editor_property(c, c == ch)
            MEL.connect_material_expressions(surf_out, "", cm, "")
            prop_out(cm, "", prop, pin)
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
    MEL.set_material_instance_parent(mi, SKINM if masked == "skin" else COATM if masked == "coat" else MASKED if masked else OPAQUE)
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
    tone = (LAYOUT.get(col or "") or {}).get("skin_tint") if t.get("_placeholder") == "rainbow" else None
    if tone:
        # arms / legs the pack leaves as the rainbow: flat in the skin tone of the character's own face
        # (Tools/Art/hero_tex_layout.py face_tone; the legs' old fallback, the arm texture, is the rainbow too here)
        return False, {"Tint": tuple(tone), "RoughOver": 0.55, "RoughOverA": 1.0, "MetalWeight": 0.0, "SpecFlat": 0.3}
    cap = any("haircap" in s for s in info.get("sections", []))  # the scalp cap (some variants file it as Lashes)
    if t.get("_placeholder") == "rainbow" and role in ("Hair", "Lashes") and not cap:
        # hair cards with no texture (CoachTexans): no alpha to cut the cards with, so they are left out (clear)
        return True, {"BaseColor": raw_tex("T_TAG_Hero_Clear", "color"), "MetalWeight": 0.0}
    if t.get("_placeholder") == "rainbow" and (role == "HairCap" or cap):
        # ... and the scalp cap under them reads as a close crop
        return False, {"Tint": (0.022, 0.018, 0.015), "RoughOver": 0.75, "RoughOverA": 1.0, "MetalWeight": 0.0}
    if t.get("_placeholder") == "rainbow" and "_legs_" in (t.get("colorTexture") or ""):
        # bare legs whose texture the pack left as the placeholder: the character's own arm skin
        arm = (t.get("colorTexture") or "").replace("_legs_", "_arms_")
        if os.path.exists(arm):
            return False, {"BaseColor": texture(arm), "MetalWeight": 0.0, "SpecFlat": 0.3}
    if t.get("_placeholder") == "rainbow" and role not in ("Head", "Eyes", "Hair", "HairCap", "Lashes", "Skin"):
        # the game's missing-texture strip: plain cloth, painted in a team colour where the game paints Mode 2
        return False, {"Mode": 2.0, "LumRef": 1.0, "Tint": (0.42, 0.42, 0.45), "RoughOver": 0.8, "RoughOverA": 1.0, "MetalWeight": 0.0}
    # flat / unset maps: the pack's flat fills, and the engine's own placeholders (default_runtime_color: the green /
    # magenta checker for a map the game fills at runtime; debug_texture_norm: the word NORMAL embossed)
    flat = (lambda q: q is None or "flatgrey" in q or "invisible" in q or "debug_texture" in q or "default_runtime" in q)
    team_cloth = kind in ("Coach", "CoachFull", "Staff", "Story", "Crew")
    P = {}
    masked = False
    if role in ("Hair", "HairCap", "Lashes"):
        masked = True
        P = {"BaseColor": texture(col), "Normal": texture(nrm, "normal") if nrm and not flat(nrm) else None}
    elif role == "Head":
        # the skin master (subsurface from the skin map's B); specular straight from the map's G
        masked = "skin" if skin_rsm else False
        P = {"BaseColor": texture(col), "Normal": texture(nrm, "normal"), "RSM": texture(skin_rsm or rsm, "linear"),
             "MetalWeight": 0.0, "SpecFlat": 0.0 if skin_rsm else 0.3}
    elif role == "Eyes":
        P = {"BaseColor": texture(col if col and not flat(col) else EYE), "RoughOver": 0.08, "RoughOverA": 1.0, "MetalWeight": 0.0}
    elif role == "Skin":
        masked = "skin"
        if kind in ("Kit", "Rig"):
            # tone 1 here; the game swaps colour and its matching RSM per player (AAthlete::PaintHero)
            P = {"BaseColor": texture(SKIN + "arm_skintone1.png"), "Normal": texture(SKIN + "arm_normal.png", "normal"),
                 "RSM": texture(SKIN + "arm_rsm_skintone1.png", "linear"), "MetalWeight": 0.0, "SpecFlat": 0.0}
        else:
            P = {"BaseColor": texture(col), "Normal": texture(nrm, "normal"), "RSM": texture(skin_rsm or rsm, "linear"),
                 "MetalWeight": 0.0, "SpecFlat": 0.0 if skin_rsm else 0.3}
            if not skin_rsm:
                masked = False
    elif role == "SkinLegs":
        masked = "skin" if skin_rsm else False
        P = {"BaseColor": texture(SKIN + "genericpadless_legs_st1.png"), "Normal": texture(nrm, "normal"),
             "RSM": texture(skin_rsm, "linear"), "MetalWeight": 0.0, "SpecFlat": 0.0 if skin_rsm else 0.3}
    elif role == "Jersey":
        # the pack's jersey folds (wrinkleNormTexture) under the in-house knit (gen_kit_cloth_maps.py) as the detail
        # normal; roughness ~0.8 from the in-house RSM (a team's licensed jersey maps read as plastic)
        wr = t.get("wrinkleNormTexture")
        P = {"Mode": 3.0, "NumOn": 1.0, "Mask": raw_tex("T_TAG_Hero_JerseyMask"),
             "Normal": texture(wr, "normal") if wr and not flat(wr) else raw_tex("T_TAG_Hero_JerseyKnitN", "normal"),
             "DetailN": raw_tex("T_TAG_Hero_JerseyKnitN", "normal") if wr and not flat(wr) else None,
             "DetailOn": 1.0 if wr and not flat(wr) else 0.0,
             "RSM": raw_tex("T_TAG_Hero_JerseyRSM", "linear"), "MetalWeight": 0.0}
    elif role == "Pants" and kind in ("Kit", "Rig"):
        # in-house tackle twill, a dull sheen (roughness ~0.66)
        P = {"Mode": 3.0, "Mask": raw_tex("T_TAG_Hero_PantsMask"), "Normal": raw_tex("T_TAG_Hero_PantsTwillN", "normal"),
             "RSM": raw_tex("T_TAG_Hero_PantsRSM", "linear"), "MetalWeight": 0.0}
    elif role == "Socks":
        P = {"Mode": 3.0, "BaseColor": texture(col), "LumRef": 0.85, "Normal": texture(nrm, "normal"), "RSM": texture(rsm, "linear"), "MetalWeight": 0.0}
    elif role == "Shell":
        # the vent / hardware normal on the shell's own detail set (TexCoord 1, kept when the job has two sets); the
        # clear-coat master (Phase 4 A9)
        masked = "coat"
        P = {"Mode": 3.0, "Mask": raw_tex("T_TAG_Hero_HelmetMask"), "Normal": texture(nrm, "normal"), "RoughOver": 0.16,
             "RoughOverA": 1.0, "MetalWeight": 0.0, "MetalAdd": 0.15, "NormalUV1": 1.0 if entry.get("uv_sets") == 2 else 0.0}
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
        if role == "Shoes" and flat(col):
            # a cleat the pack leaves for the game to colour (72 kits): a uniform packed map (mid grey, no mask), so
            # AAthlete::PaintHero paints it black or white like the packed cleats instead of one fixed grey
            P.update({"BaseColor": raw_tex("T_TAG_Hero_PackedMid", "color"), "Packed": 1.0, "Tint": (1.0, 1.0, 1.0), "Trim": (0.92, 0.92, 0.92)})
        dec = t.get("logoOverlayTexture")
        if role == "Shoes" and dec and not flat(dec) and os.path.exists(dec):
            P.update({"Decal": texture(dec), "DecalOn": 1.0})  # the cleat's marks, on its colour set (UV0, checked per set)
        if role == "Hands" and col and "glove" in col.lower():
            P["TeamGlove"] = 1.0  # a glove takes the team colour in the game (taped hands and wraps stay as they are)
    if masked == "skin":
        # the pack's tiling skin detail (normalWrapTexture: skinnoise on bodies, face_skintile on heads), on UV0 at a
        # pore-scale repeat (the pack's own detail UV set is not kept: hero_build.py keeps one set)
        wrap = t.get("normalWrapTexture")
        if wrap and not flat(wrap) and os.path.exists(wrap):
            P.update({"DetailN": texture(wrap, "normal"), "DetailOn": 1.0, "DetailTile": 10.0 if role == "Head" else 14.0})
    # the pack's packed gear maps (grey in the red channel, a region mask in blue or green): the grey overlay shading,
    # white with a white second region until the game paints them (cleats, gloves)
    lay = (LAYOUT.get(col) or {}) if col else {}
    # (crowd swarm atlases are always baked colour: a dark one, male_hardcorefan_warm_014, reads as "red only" and
    # its skin came out chalk white)
    if lay.get("layout") in ("R", "RB", "RG") and P.get("BaseColor") is not None and role not in ("Head", "Eyes", "Hair", "HairCap", "Lashes", "Skin", "SkinLegs") \
            and kind not in ("Fan", "FanProp"):
        P["Packed"] = 1.0
        if P.get("Mode", 0.0) < 0.5:
            P.setdefault("Tint", (1.0, 1.0, 1.0))
            P["Trim"] = (0.92, 0.92, 0.92)
        elif 1.5 < P.get("Mode", 0.0) < 2.5:
            # recolour by the overlay's shading: its mean, the reference that keeps the team colour at its own value
            g = min(1.0, 2.0 * float(lay.get("r", 0.5))) ** 2.2
            P["LumRef"] = max(0.02, g)
            P["Trim"] = (0.92, 0.92, 0.92)
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
    global OPAQUE, MASKED, SKINM, COATM
    unreal.AssetRegistryHelpers.get_asset_registry().search_all_assets(True)
    index = json.load(open(os.path.join(RAW, "hero_index.json")))
    OPAQUE = master("M_TAG_Hero", False)
    MASKED = master("M_TAG_HeroMasked", True)
    SKINM = master("M_TAG_HeroSkin", False, skin=True)
    COATM = master("M_TAG_HeroCoat", False, coat=True)  # helmet shells (Phase 4 A9)
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
                # a re-import replaces LOD 0 only: the reduced LODs (Tools/Art/hero_lods.py) keep their section to
                # slot maps from the old geometry, so far away a rebuilt character drew one part with another's
                # material (LeGuinDylan_Jacket GP_F: jacket in denim, jeans in the letterman). Rebuild them from LOD 0.
                lsub = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
                nl = lsub.get_lod_count(mesh)
                if nl > 1 and not lsub.regenerate_lod(mesh, nl, True, False):
                    raise RuntimeError("could not rebuild the %d reduced LODs of %s" % (nl - 1, e["name"]))
                r["lods"] = nl
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
           "skin_arm": [], "skin_arm_rsm": [], "skin_legs": [], "eyes": [], "items": []}
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
        ar = texture(SKIN + "arm_rsm_skintone%d.png" % n, "linear")  # each tone's own roughness / specular / scatter
        cat["skin_arm_rsm"].append(ar.get_path_name() if ar else "")
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
        if e["name"] in UNUSABLE and os.environ.get("TAG_HERO_ALL") != "1":  # TAG_HERO_ALL=1: list them anyway (to look)
            REPORT.setdefault("left_out", {})[e["name"]] = UNUSABLE[e["name"]]
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
