# Unreal Editor Python: the realistic turf. Imports the in-house maps of Tools/Art/gen_turf_textures.py and builds
#   /Game/TAG/Materials/M_TagTurf2       the ground: ONE material for the whole playing surface, mapped from the world
#                                        position (grass, mowing stripes, wear, yard lines, end zones with wordmarks,
#                                        midfield logo, white border, apron, rain and snow)
#   /Game/TAG/Materials/M_TagGrassBlade  the close-up blades (Render/Stadium.cpp BuildGrass3D): the same colour as the
#                                        ground under each blade, and the vertex shader that wraps the blade patch
#                                        around the camera's subject
# Run (game and editor closed):
#   UnrealEditor-Cmd.exe TAGGridiron.uproject -run=pythonscript -script="<this file>" -unattended -nosplash -nullrhi
# M_TagTurf (ue_build_materials.py) is left alone: the game falls back to it when M_TagTurf2 is missing or with
# -TagOldTurf.
#
# World layout the shaders assume (Render/Coords.h): the stadium actor at the origin, 1 yd = 91.44 cm, the field from
# x = -10 yd to 110 yd (goal lines at 0 and 100) and y = +-26.667 yd.
import os

import unreal

PROJ_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
RAW = os.path.join(PROJ_DIR, "RawArt")
TEX_DIR = "/Game/TAG/Textures"
MAT_DIR = "/Game/TAG/Materials"
tools = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
F2 = unreal.CustomMaterialOutputType.CMOT_FLOAT2
F3 = unreal.CustomMaterialOutputType.CMOT_FLOAT3
F4 = unreal.CustomMaterialOutputType.CMOT_FLOAT4
ST = unreal.MaterialSamplerType


def import_tex(name, kind):
    """kind: gray (one linear channel), linear (colour data, no sRGB), normal."""
    task = unreal.AssetImportTask()
    task.filename = os.path.join(RAW, name + ".png")
    task.destination_path = TEX_DIR
    task.destination_name = name
    task.replace_existing = True
    task.automated = True
    task.save = False
    tools.import_asset_tasks([task])
    tex = unreal.load_asset(TEX_DIR + "/" + name)
    if kind == "normal":
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        tex.set_editor_property("srgb", False)
    elif kind == "gray":
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_GRAYSCALE)
        tex.set_editor_property("srgb", False)
    else:
        tex.set_editor_property("srgb", False)
    EAL.save_loaded_asset(tex, only_if_is_dirty=False)
    return tex


FIELD_UV = """float2 yd = P.xy / 91.44;
float2 f = float2((yd.x + 10.0) / 120.0, yd.y / 53.3333 + 0.5);
return float2(lerp(f.x, 1.0 - f.x, UVFlipU), lerp(f.y, 1.0 - f.y, UVFlipV));"""

# rgb = the paint's colour, a = how much of the turf it covers (before the blades break it up)
PAINT = """float2 yd = P.xy / 91.44;
float FW = 53.3333;
float hx = abs(yd.x - 50.0);
float hy = abs(yd.y);
float inField = (hx < 60.0 && hy < FW * 0.5) ? 1.0 : 0.0;
float3 col = LineColor;
float a = 0.0;
// end zones: the team's colour and its wordmark across the width, tops toward the end line
float home = yd.x > 50.0 ? 1.0 : 0.0;
float ezd = 60.0 - hx;
float inEz = inField * (ezd < 10.0 ? 1.0 : 0.0);
float3 ezA = lerp(AwayA, HomeA, home);
float3 ezB = lerp(AwayB, HomeB, home);
float idx = lerp(AwayIdx, HomeIdx, home);
float2 wq = float2((home * 2.0 - 1.0) * yd.y / WordLen + 0.5, (ezd - 5.0) / WordH + 0.5);
wq = float2(lerp(wq.x, 1.0 - wq.x, WordFlipU), lerp(wq.y, 1.0 - wq.y, WordFlipV));
float win = (wq.x > 0.0 && wq.x < 1.0 && wq.y > 0.0 && wq.y < 1.0) ? 1.0 : 0.0;
float2 wcell = float2(fmod(idx, 2.0), floor(idx / 2.0));
float2 wsc = float2(2.0, 16.0);
float wm = Texture2DSampleGrad(Words, WordsSampler, (wcell + saturate(wq)) / wsc, ddx(wq) / wsc, ddy(wq) / wsc).r * win;
wm = saturate((wm - 0.5) * 3.0 + 0.5);
col = lerp(col, lerp(ezA, ezB, wm), inEz);
a = max(a, inEz * EzAmount);
// yard lines, hash marks, numbers, sidelines
float L = Texture2DSample(Lines, LinesSampler, FUV).r * inField * LinesAmount;
col = lerp(col, LineColor, L);
a = max(a, L);
// midfield logo (the home team's mark: R fill, G ring and badge), over the 50
float2 lq = float2(yd.x - 50.0, yd.y) / LogoSize + 0.5;
lq = lerp(lq, 1.0 - lq, LogoFlip);
float lin = (lq.x > 0.0 && lq.x < 1.0 && lq.y > 0.0 && lq.y < 1.0) ? LogoAmount : 0.0;
float2 lcell = float2(fmod(HomeIdx, 8.0), floor(HomeIdx / 8.0));
float2 lsc = float2(8.0, 4.0);
float2 lm = Texture2DSampleGrad(Logos, LogosSampler, (lcell + saturate(lq)) / lsc, ddx(lq) / lsc, ddy(lq) / lsc).rg * lin;
lm = saturate((lm - 0.5) * 3.0 + 0.5);
float la = max(lm.x, lm.y);
col = lerp(col, lerp(LogoLine, LogoFill, lm.x), la);
a = max(a, la * 0.95);
// the 6 ft white border outside the sidelines and end lines, and the yellow coaching line between the 32s
float bd = max(hx - 60.0, hy - FW * 0.5);
float fw = max(fwidth(bd), 0.0001);
float border = saturate(bd / fw + 0.5) * saturate((2.0 - bd) / fw + 0.5) * BorderAmount;
col = lerp(col, LineColor, border);
a = max(a, border * 0.97);
float cl = saturate((0.06 - abs(hy - FW * 0.5 - 2.3)) / fw + 0.5) * (hx < 18.0 ? 1.0 : 0.0) * BorderAmount;
col = lerp(col, float3(0.45, 0.38, 0.02), cl);
a = max(a, cl * 0.95);
return float4(col, a);"""

# rgb = base colour, a = roughness
GROUND = """float2 yd = P.xy / 91.44;
float FW = 53.3333;
float hx = abs(yd.x - 50.0);
float hy = abs(yd.y);
float inField = (hx < 60.0 && hy < FW * 0.5) ? 1.0 : 0.0;
float bd = max(hx - 60.0, hy - FW * 0.5);
// cloud noise at three unrelated scales and angles: nothing repeats from the broadcast camera
float3 n1 = Texture2DSample(Macro, MacroSampler, P.xy / 3900.0).rgb;
float3 n2 = Texture2DSample(Macro, MacroSampler, float2(P.y, -P.x) / 1270.0 + 0.37).rgb;
float2 pr = float2(P.x * 0.866 - P.y * 0.5, P.x * 0.5 + P.y * 0.866);
float n3 = Texture2DSample(Macro, MacroSampler, pr / 7100.0 + 0.61).r;
float big = n1.r * 0.4 + n2.g * 0.3 + n3 * 0.3;
float fine = n2.b;
float nearF = saturate(1.0 - (Dist - 500.0) / 4500.0);
float midF = saturate(1.0 - (Dist - 2500.0) / 9000.0);
// the blades from above (1.5 m a tile) up close; the same map 7.5 times larger as the grain the game camera resolves
float4 b1 = Texture2DSample(BladesD, BladesDSampler, P.xy / 150.0);
float4 b2 = Texture2DSample(BladesD, BladesDSampler, float2(P.y, P.x) / 1130.0 + 0.21);
float h = lerp(0.5, b1.a, nearF);
float3 det = lerp(float3(1.0, 1.0, 1.0), b1.rgb * DetailGain, nearF);
det *= lerp(1.0, 0.82 + 0.36 * b2.a, midF * (1.0 - 0.6 * nearF));
float isBlade = Blade.z;
if (isBlade > 0.5)
{
    // a 3D blade: Blade.x runs 0 at the root to 1 at the tip, Blade.y is the blade's own random number
    nearF = 1.0;
    h = Blade.x;
    float sh = lerp(0.5, 1.25, Blade.x) * lerp(0.85, 1.15, Blade.y);
    det = float3(sh * lerp(0.92, 1.18, Blade.y), sh, sh * 0.9);
}
float3 g = GrassColor * Tint;
g *= lerp(0.86, 1.14, big);
// patchy growth a metre or two across (what the high camera resolves as the turf's grain), kept off the blade scale
float patch = Texture2DSample(Macro, MacroSampler, P.xy / 640.0 + 0.53).b;
g *= lerp(0.9, 1.1, patch) * lerp(1.0, 0.94 + 0.12 * fine, 1.0 - nearF);
g.r *= lerp(0.9, 1.16, n1.g);
g.b *= lerp(0.9, 1.1, n2.r);
// mowing stripes, 5 yd: the mower runs sideline to sideline and lays the blades over, alternate bands toward either
// sideline. A band leaning away from the viewer shows the blades' lit sides, one leaning toward him their shaded tips:
// the contrast follows how much the view runs across the field (strong from either sideline, the light and dark bands
// swapping sides, faint from the end zones), plus a little that the mowing leaves in the colour itself
float fx = frac(yd.x / 5.0);
float s = (fmod(floor(yd.x / 5.0) + 400.0, 2.0) < 0.5 ? 1.0 : -1.0) * inField * saturate(min(fx, 1.0 - fx) * 60.0);
float2 v2 = CamVec.xy / max(length(CamVec.xy), 0.3);
g *= 1.0 + StripeAmount * s * (0.035 - 0.19 * v2.y) * (1.0 - 0.7 * saturate(Snow * 2.0));
// wear: stressed grass yellows, then thins to dirt between the blades. The season's map (Wear x WearAmount), or the
// game's live one (WearLive 1: WearRT, R wear, G trampled snow, B mud, stamped by Render/Stadium.cpp as the game goes)
float4 wl = Texture2DSample(WearRT, WearRTSampler, FUV) * WearLive;
float w = lerp(Texture2DSample(Wear, WearSampler, FUV).r * WearAmount, wl.r, WearLive) * inField;
float tramp = saturate(wl.g) * inField;
float mud = saturate(wl.b) * inField;
g = lerp(g, g * float3(1.4, 1.08, 0.7), saturate(w * 1.6) * 0.55);
float bare = saturate((w * 1.25 + (fine - 0.5) * 0.5 - 0.55 - (h - 0.5) * 0.5 * nearF) * 2.2) * step(0.02, w);
bare = max(bare, saturate(mud * 1.2 + (fine - 0.5) * 0.4) * step(0.02, mud));  // churned to mud: bare ground
float3 c = g * det;
float3 dirt = lerp(DirtColor, g, 0.3) * lerp(0.85, 1.15, fine) * lerp(1.0, 0.6 + 0.8 * b1.g, nearF * (1.0 - isBlade));
c = lerp(c, dirt, bare);
c *= lerp(1.0, 0.72, mud * Wet);  // wet churned mud is darker still
c *= lerp(1.0, ApronTint, saturate((bd - 2.0) * 4.0));
// paint lies on the blades: worn off with the turf, uneven coats, and up close the gaps between blades stay dark
// and the edge follows the blades
float pa = Paint.a * (1.0 - bare * 0.75) * lerp(0.82, 1.0, n2.b);
float pn = saturate((pa + (h - 0.5) * 0.7 - 0.5) * 5.0 + 0.5);
pa = lerp(pa, pn, nearF);
float lum = clamp(dot(det, float3(0.3333, 0.3333, 0.3333)), 0.45, 1.3);
c = lerp(c, Paint.rgb * lerp(1.0, lum, 0.8) * lerp(0.92, 1.06, big), pa);
// rain: darker and deeper green (water fills the gaps between the blades), a soft sheen rather than a mirror (the
// blades break every reflection up), standing water only where the turf is worn to dirt
float wetv = Wet * lerp(0.7, 1.0, n1.b);
float cl0 = dot(c, float3(0.3333, 0.3333, 0.3333));
c = lerp(c, max(cl0 + (c - cl0) * 1.2, 0.0), wetv) * (1.0 - 0.35 * wetv);
float rough = lerp(lerp(0.9, 0.8, pa), lerp(0.7, 0.6, n2.r), wetv);
rough = lerp(rough, lerp(0.95, 0.3, wetv), bare);
rough = lerp(rough, 0.15, mud * wetv * 0.6);  // puddles in the mud
// snow: a dusting the turf shows through, deeper in drifts and off the field, the blade tips standing out of it,
// trampled away where the game is played and swept off the lines. Trampled by the season's wear map, or (WearLive)
// by the game's own lanes and worn spots
float sn = saturate(Snow * lerp(0.6, 1.4, big) + (patch - 0.5) * 0.7 * Snow + (fine - 0.5) * 0.5 * Snow + saturate(bd * 0.5) * 0.3 * Snow - (h - 0.5) * 0.6 * nearF) * step(0.01, Snow);
float tr = lerp(saturate(w * 1.6 + (patch - 0.5) * 0.4), saturate(tramp * 0.9 + w * 0.5 + (patch - 0.5) * 0.3), WearLive);
sn *= 1.0 - tr * 0.75;
// the crews keep the lines swept: a band of turf either side of the paint (the line mask's mip 5, ~40 cm texels);
// SweepAmount 0: only the paint shows through, as before
float sweep = saturate(Texture2DSampleLevel(Lines, LinesSampler, FUV, 5.0).r * 6.0) * inField * SweepAmount;
sn *= 1.0 - max(Paint.a * 0.85 * inField, sweep * 0.92);
// snow that holds detail: drifts and hollows, and grey slush where it is trodden (SnowDetail 0: the flat grey as before)
float3 snc = SnowColor * lerp(1.0, lerp(0.84, 1.04, big) * lerp(0.95, 1.03, fine), SnowDetail);
float slush = tr * step(0.01, Snow) * SnowDetail;
c = lerp(c, lerp(c, SnowColor * float3(0.42, 0.41, 0.40), 0.6), slush * 0.7);
c = lerp(c, snc, sn);
rough = lerp(rough, 0.72, sn);
rough = lerp(rough, 0.45, slush * 0.5);
return float4(c, rough);"""

NORMAL = """float nearF = saturate(1.0 - (Dist - 500.0) / 4500.0);
float2 yd = P.xy / 91.44;
float inField = (abs(yd.x - 50.0) < 60.0 && abs(yd.y) < 26.6667) ? 1.0 : 0.0;
float s = (fmod(floor(yd.x / 5.0) + 400.0, 2.0) < 0.5 ? 1.0 : -1.0) * inField;
float3 n = lerp(float3(0.0, 0.0, 1.0), BN, nearF * NormalAmount);
n.y += s * StripeTilt;
// snow lies in soft lumps over the blades and the stripes (SnowBump 0: as before)
float sb = SnowBump * saturate(Snow * 1.5);
if (sb > 0.0)
{
    float2 q = P.xy / 260.0 + 0.17;
    float h0 = Texture2DSample(Macro, MacroSampler, q).g;
    float hx = Texture2DSample(Macro, MacroSampler, q + float2(0.03, 0.0)).g;
    float hy = Texture2DSample(Macro, MacroSampler, q + float2(0.0, 0.03)).g;
    n = lerp(n, normalize(float3((h0 - hx) * sb, (h0 - hy) * sb, 1.0)), saturate(Snow * 1.5));
}
return normalize(n);"""

# The blade patch: PatchSize square of blades built once at the origin (the component never moves). TexCoord 1 = the
# vertex's offset from its blade's foot (x, y), TexCoord 2 = (offset z, the blade's random number); the foot is the
# vertex less its offset (the UV channels are half floats: fine for a 4 cm offset, too coarse for a 15 m position).
# The foot is wrapped into the square around GrassCenter, so the patch follows the camera's subject without a blade
# ever moving on the ground; blades shrink to nothing at the rim and on worn turf.
BLADE_WPO = """float2 Root = WP.xy - OffXY;
float2 C = GrassCenter.xy;
float2 rel = frac((Root - C) / PatchSize + 0.5) - 0.5;
float2 wr = C + rel * PatchSize;
float rim = saturate((0.5 - length(rel)) * 6.0);
float2 yd = wr / 91.44;
float hx = abs(yd.x - 50.0);
float hy = abs(yd.y);
float keep = (hx < 71.0 && hy < 34.0) ? 1.0 : 0.0;
float inField = (hx < 60.0 && hy < 26.6667) ? 1.0 : 0.0;
float2 f = float2((yd.x + 10.0) / 120.0, yd.y / 53.3333 + 0.5);
f = float2(lerp(f.x, 1.0 - f.x, UVFlipU), lerp(f.y, 1.0 - f.y, UVFlipV));
float w = lerp(Texture2DSampleLevel(Wear, WearSampler, f, 0).r * WearAmount, Texture2DSampleLevel(WearRT, WearRTSampler, f, 0).r, WearLive) * inField;
float k = Grow * rim * keep * saturate(1.0 - w * 1.5);
float3 o = float3(OffXY, OffZR.x) * k;
float ph = Tm * 2.1 + wr.x * 0.021 + wr.y * 0.017 + OffZR.y * 6.283;
o.xy += WindVec.xy * (0.6 + 0.4 * sin(ph)) * T * T * k;
return float3(wr, ZBase) + o - WP;"""


def build(name, tex, blade):
    path = MAT_DIR + "/" + name
    if EAL.does_asset_exist(path):
        m = unreal.load_asset(path)
        MEL.delete_all_material_expressions(m)
    else:
        m = tools.create_asset(name, MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
    y = [0]

    def node(cls, x=-1500):
        e = MEL.create_material_expression(m, cls, x, y[0])
        y[0] += 120
        return e

    params = {}

    def scalar(n, v):
        if n not in params:
            e = node(unreal.MaterialExpressionScalarParameter)
            e.set_editor_property("parameter_name", n)
            e.set_editor_property("default_value", v)
            params[n] = e
        return params[n]

    def vector(n, v):
        if n not in params:
            e = node(unreal.MaterialExpressionVectorParameter)
            e.set_editor_property("parameter_name", n)
            e.set_editor_property("default_value", unreal.LinearColor(v[0], v[1], v[2], 1))
            params[n] = e
        return params[n]

    def texobj(n, t, stype):
        if n not in params:
            e = node(unreal.MaterialExpressionTextureObjectParameter)
            e.set_editor_property("parameter_name", n)
            e.set_editor_property("texture", t)
            e.set_editor_property("sampler_type", stype)
            params[n] = e
        return params[n]

    def custom(code, out, ins, desc, x=-800):
        c = MEL.create_material_expression(m, unreal.MaterialExpressionCustom, x, y[0])
        y[0] += 250
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

    def link(a, b, pin, out=""):
        if not MEL.connect_material_expressions(a, out, b, pin):
            raise RuntimeError("%s: could not connect pin %s" % (name, pin))

    SC = {"UVFlipU": 0.0, "UVFlipV": 0.0, "HomeIdx": 0.0, "AwayIdx": 1.0, "WordLen": 40.0, "WordH": 6.2,
          "WordFlipU": 0.0, "WordFlipV": 0.0, "EzAmount": 0.92, "LinesAmount": 1.0, "LogoSize": 15.0, "LogoFlip": 1.0,
          "LogoAmount": 1.0, "BorderAmount": 1.0, "DetailGain": 2.0, "StripeAmount": 1.0, "WearAmount": 0.0,
          "ApronTint": 0.72, "Wet": 0.0, "Snow": 0.0, "NormalAmount": 0.9, "StripeTilt": 0.08, "Specular": 0.3,
          "PatchSize": 1500.0, "Grow": 1.0, "ZBase": 0.5,
          # Research/NextLevel/03 A2 / A5 (Render/Stadium.cpp sets them; each default is the look before them): the
          # live wear map off, no swept lines, flat snow, no snow lumps
          "WearLive": 0.0, "SweepAmount": 0.0, "SnowDetail": 0.0, "SnowBump": 0.0}
    VC = {"LineColor": (0.72, 0.72, 0.70), "HomeA": (0.02, 0.05, 0.3), "HomeB": (0.72, 0.72, 0.70),
          "AwayA": (0.3, 0.03, 0.03), "AwayB": (0.72, 0.72, 0.70), "LogoFill": (0.72, 0.72, 0.70),
          "LogoLine": (0.02, 0.05, 0.3), "GrassColor": (0.0135, 0.0306, 0.0063), "Tint": (0.96, 0.96, 1.15),
          "DirtColor": (0.048, 0.034, 0.019), "GrassCenter": (4572.0, 0.0, 0.0), "WindVec": (0.25, 0.1, 0.0),
          "SnowColor": (0.62, 0.64, 0.68)}

    wp = node(unreal.MaterialExpressionWorldPosition)
    fuv = custom(FIELD_UV, F2, ["P", "UVFlipU", "UVFlipV"], "TAG field uv")
    link(wp, fuv, "P")
    for n in ("UVFlipU", "UVFlipV"):
        link(scalar(n, SC[n]), fuv, n)

    p_s = ["HomeIdx", "AwayIdx", "WordLen", "WordH", "WordFlipU", "WordFlipV", "EzAmount", "LinesAmount", "LogoSize",
           "LogoFlip", "LogoAmount", "BorderAmount"]
    p_v = ["LineColor", "HomeA", "HomeB", "AwayA", "AwayB", "LogoFill", "LogoLine"]
    paint = custom(PAINT, F4, ["P", "FUV", "Lines", "Logos", "Words"] + p_v + p_s, "TAG field paint")
    link(wp, paint, "P")
    link(fuv, paint, "FUV")
    link(texobj("Lines", tex["lines"], ST.SAMPLERTYPE_LINEAR_GRAYSCALE), paint, "Lines")
    link(texobj("Logos", tex["logos"], ST.SAMPLERTYPE_LINEAR_COLOR), paint, "Logos")
    link(texobj("Words", tex["words"], ST.SAMPLERTYPE_LINEAR_GRAYSCALE), paint, "Words")
    for n in p_v:
        link(vector(n, VC[n]), paint, n)
    for n in p_s:
        link(scalar(n, SC[n]), paint, n)

    g_s = ["DetailGain", "StripeAmount", "WearAmount", "ApronTint", "Wet", "Snow", "WearLive", "SweepAmount", "SnowDetail"]
    g_v = ["GrassColor", "Tint", "DirtColor", "SnowColor"]
    ground = custom(GROUND, F4, ["Paint", "P", "FUV", "Dist", "CamVec", "Blade", "BladesD", "Macro", "Wear", "WearRT", "Lines"] + g_v + g_s,
                    "TAG turf")
    link(paint, ground, "Paint")
    link(wp, ground, "P")
    link(fuv, ground, "FUV")
    link(node(unreal.MaterialExpressionPixelDepth), ground, "Dist")
    link(node(unreal.MaterialExpressionCameraVectorWS), ground, "CamVec")
    if blade:
        vc = node(unreal.MaterialExpressionVertexColor)
        bl = custom("return float4(G, R, 1.0, 0.0);", F4, ["R", "G"], "TAG blade")
        link(vc, bl, "R", "R")
        link(vc, bl, "G", "G")
    else:
        bl = node(unreal.MaterialExpressionConstant4Vector)
        bl.set_editor_property("constant", unreal.LinearColor(0, 0, 0, 0))
    link(bl, ground, "Blade")
    link(texobj("BladesD", tex["blades_d"], ST.SAMPLERTYPE_LINEAR_COLOR), ground, "BladesD")
    link(texobj("Macro", tex["macro"], ST.SAMPLERTYPE_LINEAR_COLOR), ground, "Macro")
    link(texobj("Wear", tex["wear"], ST.SAMPLERTYPE_LINEAR_GRAYSCALE), ground, "Wear")
    # the live wear map: a render target the game sets (RGBA16f); the macro noise stands in as the default, unused
    # while WearLive is 0
    link(texobj("WearRT", tex["macro"], ST.SAMPLERTYPE_LINEAR_COLOR), ground, "WearRT")
    link(texobj("Lines", tex["lines"], ST.SAMPLERTYPE_LINEAR_GRAYSCALE), ground, "Lines")
    for n in g_v:
        link(vector(n, VC[n]), ground, n)
    for n in g_s:
        link(scalar(n, SC[n]), ground, n)

    rgb = MEL.create_material_expression(m, unreal.MaterialExpressionComponentMask, -300, 0)
    for ch in "rgba":
        rgb.set_editor_property(ch, ch in "rgb")
    link(ground, rgb, "")
    MEL.connect_material_property(rgb, "", unreal.MaterialProperty.MP_BASE_COLOR)
    ra = MEL.create_material_expression(m, unreal.MaterialExpressionComponentMask, -300, 150)
    for ch in "rgba":
        ra.set_editor_property(ch, ch == "a")
    link(ground, ra, "")
    MEL.connect_material_property(ra, "", unreal.MaterialProperty.MP_ROUGHNESS)
    # turf is a forest of blades: seen at a grazing angle (the broadcast camera across the field, the floodlights low
    # on the far side) the blades shadow and hide one another's sheen, so the specular falls away toward the horizon
    # instead of the Fresnel rise a flat floor would show (the grey veil over a night or wet field)
    spec = custom("return Specular * lerp(0.25, 1.0, saturate(abs(CamVec.z) * 2.5));", unreal.CustomMaterialOutputType.CMOT_FLOAT1,
                  ["Specular", "CamVec"], "TAG turf sheen")
    link(scalar("Specular", SC["Specular"]), spec, "Specular")
    link(node(unreal.MaterialExpressionCameraVectorWS), spec, "CamVec")
    MEL.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)

    if not blade:
        nuv = custom("return P.xy / 150.0;", F2, ["P"], "TAG blade uv")
        link(wp, nuv, "P")
        bn = node(unreal.MaterialExpressionTextureSampleParameter2D, -1000)
        bn.set_editor_property("parameter_name", "BladesN")
        bn.set_editor_property("texture", tex["blades_n"])
        bn.set_editor_property("sampler_type", ST.SAMPLERTYPE_NORMAL)
        link(nuv, bn, "UVs")
        nrm = custom(NORMAL, F3, ["BN", "P", "Dist", "NormalAmount", "StripeTilt", "Macro", "Snow", "SnowBump"], "TAG turf normal")
        link(bn, nrm, "BN", "RGB")
        link(wp, nrm, "P")
        link(node(unreal.MaterialExpressionPixelDepth), nrm, "Dist")
        link(params["Macro"], nrm, "Macro")
        for n in ("NormalAmount", "StripeTilt", "Snow", "SnowBump"):
            link(scalar(n, SC[n]), nrm, n)
        MEL.connect_material_property(nrm, "", unreal.MaterialProperty.MP_NORMAL)
    else:
        w_s = ["PatchSize", "Grow", "ZBase", "UVFlipU", "UVFlipV", "WearAmount", "WearLive"]
        wpo = custom(BLADE_WPO, F3, ["OffXY", "OffZR", "T", "WP", "Tm", "Wear", "WearRT", "GrassCenter", "WindVec"] + w_s, "TAG blade patch")
        for i, pin in ((1, "OffXY"), (2, "OffZR")):
            tc = node(unreal.MaterialExpressionTextureCoordinate)
            tc.set_editor_property("coordinate_index", i)
            link(tc, wpo, pin)
        link(node(unreal.MaterialExpressionVertexColor), wpo, "T", "G")
        link(wp, wpo, "WP")
        link(node(unreal.MaterialExpressionTime), wpo, "Tm")
        link(params["Wear"], wpo, "Wear")
        link(params["WearRT"], wpo, "WearRT")
        link(vector("GrassCenter", VC["GrassCenter"]), wpo, "GrassCenter")
        link(vector("WindVec", VC["WindVec"]), wpo, "WindVec")
        for n in w_s:
            link(scalar(n, SC[n]), wpo, n)
        MEL.connect_material_property(wpo, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    MEL.recompile_material(m)
    EAL.save_loaded_asset(m, only_if_is_dirty=False)
    try:
        st = MEL.get_statistics(m)
        unreal.log("TAG turf %s: vs %d ps %d instructions, %d samplers" % (name, st.num_vertex_shader_instructions,
                                                                             st.num_pixel_shader_instructions, st.num_samplers))
    except Exception as e:
        unreal.log_warning("TAG turf %s statistics: %s" % (name, e))
    return m


def dump_plane_uv():
    """Which way the engine plane's UVs run (the old turf material mapped the line texture by them)."""
    try:
        sm = unreal.load_asset("/Engine/BasicShapes/Plane")
        d = sm.get_static_mesh_description(0)
        out = []
        for i in range(d.get_vertex_instance_count()):
            vi = unreal.VertexInstanceID(i)
            p = d.get_vertex_position(d.get_vertex_instance_vertex(vi))
            uv = d.get_vertex_instance_uv(vi, 0)
            out.append("(%.0f %.0f)->(%.2f %.2f)" % (p.x, p.y, uv.x, uv.y))
        unreal.log("TAG plane uv: " + " ".join(sorted(set(out))))
    except Exception as e:
        unreal.log_warning("TAG plane uv: %s" % e)


def main():
    only_mats = os.environ.get("TAG_TURF_MATS") == "1"   # rebuild the two materials, keep the imported textures
    names = {"blades_d": ("T_TagTurfBlades_D", "linear"), "blades_n": ("T_TagTurfBlades_N", "normal"),
             "macro": ("T_TagTurfMacro", "linear"), "logos": ("T_TagFieldLogos", "linear"),
             "words": ("T_TagFieldWords", "gray"), "lines": ("T_TagFieldLines", "gray")}
    tex = {}
    for k, (n, kind) in names.items():
        have = EAL.does_asset_exist(TEX_DIR + "/" + n)
        tex[k] = unreal.load_asset(TEX_DIR + "/" + n) if (only_mats and have) else import_tex(n, kind)
        if tex[k] is None:
            raise RuntimeError("missing texture " + n)
    tex["wear"] = unreal.load_asset(TEX_DIR + "/T_TurfWear")
    if tex["wear"] is None:
        raise RuntimeError("missing /Game/TAG/Textures/T_TurfWear (ue_build_materials.py)")
    dump_plane_uv()
    build("M_TagTurf2", tex, False)
    build("M_TagGrassBlade", tex, True)
    unreal.log("TAG turf materials built OK")


main()
