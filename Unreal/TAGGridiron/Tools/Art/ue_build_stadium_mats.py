# Unreal Editor Python: stadium display materials (LED ribbon boards, video boards, suite glass).
# Run: UnrealEditor-Cmd.exe TAGGridiron.uproject -run=pythonscript -script="<this file>"
#
# M_TagLED: emissive panel. Emissive = lerp(Color, ColorB, segment) * Intensity, where the segment alternates every
# 1/Count of U and scrolls at Speed (ribbon boards run team-colour panels around the bowl); Count 0 is a solid panel.
# M_TagPrecip: the camera-local rain and snow (below). TAG_STADIUM_MATS=precip builds only that one.
import os

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


# M_TagPrecip (Research/NextLevel/03 A2): rain and snow you can see, built the way the close-up grass is. The mesh
# (Render/Stadium.cpp BuildPrecip) is one quad per flake or drop, its four corners all at the point it was made at
# (TexCoord 0 the corner 0/1, TexCoord 1 two random numbers); the component never moves. The vertex shader lets each
# point fall (and drift with the wind, and flutter if it is a flake), wraps it into the box around Center (the game
# moves that with the shot every frame), and spreads the quad around it turned to the camera: a flake a disc never
# under MinPx pixels, a drop a streak along its fall at least MinPx wide and 3 x MinPx long. A point beyond Intensity
# (its first random number) stays a point: the weather builds by showing more of them. Quads shrink to nothing at the
# box's faces (no pop at the wrap) and close to the lens. Unlit, additive, two-sided; the brightness is set after
# exposure (divided by the eye adaptation) so a flake reads the same by day and under the lights.
PRECIP_WPO = r"""
float3 home = WP;
float r1 = R.x;
float r2 = R.y;
float keep = r1 < Intensity ? 1.0 : 0.0;
float spd = Fall * lerp(0.8, 1.2, r2);
float3 p = home + float3(Wind.xy * Tm, -spd * Tm);
float fl = 1.0 - Streak;
p.xy += Swirl * fl * float2(sin(Tm * (0.9 + r1) + r2 * 6.2832), cos(Tm * (0.7 + r2) + r1 * 6.2832));
float3 rel = frac((p - Center) / Box + 0.5) - 0.5;
float3 w = Center + rel * Box;
float3 ar = abs(rel);
float rim = saturate((0.5 - max(ar.x, max(ar.y, ar.z))) * 12.0);
float3 tc = CamPos - w;
float d = max(length(tc), 1.0);
tc /= d;
float px = PxCm * d;
float k = keep * rim * saturate((d - Near) / max(Near, 1.0));
float3 rt = float3(1.0, 0.0, 0.0);
float3 up = float3(0.0, 0.0, 1.0);
float2 sz = float2(1.0, 1.0);
if (Streak > 0.5)
{
    float3 v = normalize(float3(Wind.xy, -spd));
    float3 c1 = cross(v, tc);
    float l1 = length(c1);
    rt = l1 > 0.001 ? c1 / l1 : float3(1.0, 0.0, 0.0);
    up = v;
    sz = float2(max(Size.x, MinPx * px), max(Size.y, MinPx * 3.0 * px));
}
else
{
    float3 c2 = cross(float3(0.0, 0.0, 1.0), tc);
    float l2 = length(c2);
    rt = l2 > 0.001 ? c2 / l2 : float3(1.0, 0.0, 0.0);
    up = cross(tc, rt);
    float s = max(Size.x * lerp(0.6, 1.4, r2), MinPx * px);
    sz = float2(s, s);
}
float3 o = (rt * (UV.x - 0.5) * sz.x + up * (UV.y - 0.5) * sz.y) * k;
return w + o - WP;
"""

PRECIP_EMIS = r"""
float2 q = UV - 0.5;
float a = Streak > 0.5 ? saturate(1.0 - abs(q.x) * 2.0) * sin(saturate(UV.y) * 3.14159) : saturate(1.0 - length(q) * 2.0);
a = Streak > 0.5 ? a * a : a;  // (a flake a firm dot: squared, the 1.6 px flakes vanished at TV distance)
return Color * (Bright * a / max(EA, 0.000001));
"""


def build_precip():
    m = new_material("M_TagPrecip")
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("two_sided", True)
    tpass = getattr(unreal, "MaterialTranslucencyPass", None)
    for prop, val in (("enable_responsive_aa", True),  # TSR: thin moving streaks are not smeared by the history
                      ("translucency_pass", getattr(tpass, "MTP_BEFORE_DOF", None) if tpass else None)):  # near flakes blur in the f/1.2 close shots
        if val is None:
            continue
        try:
            m.set_editor_property(prop, val)
        except Exception as e:
            unreal.log_warning("TAG precip: %s not set (%s)" % (prop, e))
    y = [0]

    def node(cls, x=-1300):
        e = MEL.create_material_expression(m, cls, x, y[0])
        y[0] += 120
        return e

    def custom(code, out, ins, desc):
        c = MEL.create_material_expression(m, unreal.MaterialExpressionCustom, -600, y[0])
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
            raise RuntimeError("M_TagPrecip: could not connect pin %s" % pin)

    F3 = unreal.CustomMaterialOutputType.CMOT_FLOAT3
    sc = {"Intensity": 1.0, "Fall": 110.0, "Streak": 0.0, "Swirl": 25.0, "PxCm": 0.0004, "MinPx": 1.6, "Near": 150.0, "Bright": 0.6}
    vc = {"Center": (4572.0, 0.0, 1200.0, 0.0), "Box": (4000.0, 4000.0, 2500.0, 0.0), "Wind": (0.0, 0.0, 0.0, 0.0),
          "Size": (1.2, 1.2, 0.0, 0.0), "Color": (0.95, 0.97, 1.0, 1.0)}
    params = {}

    def par(n):
        if n not in params:
            if n in sc:
                params[n] = scalar(m, n, sc[n], -1300, y[0])
            else:
                params[n] = vector(m, n, vc[n], -1300, y[0])
            y[0] += 120
        return params[n]

    wpo = custom(PRECIP_WPO, F3, ["WP", "R", "UV", "Tm", "CamPos", "Intensity", "Fall", "Wind", "Streak", "Swirl", "Center", "Box",
                                  "PxCm", "MinPx", "Near", "Size"], "TAG precip volume")
    link(node(unreal.MaterialExpressionWorldPosition), wpo, "WP")
    tc1 = node(unreal.MaterialExpressionTextureCoordinate)
    tc1.set_editor_property("coordinate_index", 1)
    link(tc1, wpo, "R")
    link(node(unreal.MaterialExpressionTextureCoordinate), wpo, "UV")
    link(node(unreal.MaterialExpressionTime), wpo, "Tm")
    link(node(unreal.MaterialExpressionCameraPositionWS), wpo, "CamPos")
    for n in ("Intensity", "Fall", "Wind", "Streak", "Swirl", "Center", "Box", "PxCm", "MinPx", "Near", "Size"):
        link(par(n), wpo, n)
    MEL.connect_material_property(wpo, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    emis = custom(PRECIP_EMIS, F3, ["UV", "Streak", "Color", "Bright", "EA"], "TAG precip shape")
    link(node(unreal.MaterialExpressionTextureCoordinate), emis, "UV")
    for n in ("Streak", "Color", "Bright"):
        link(par(n), emis, n)
    link(node(unreal.MaterialExpressionEyeAdaptation), emis, "EA")
    MEL.connect_material_property(emis, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.recompile_material(m)
    EAL.save_loaded_asset(m)
    try:
        st = MEL.get_statistics(m)
        unreal.log("TAG precip M_TagPrecip: vs %d ps %d instructions" % (st.num_vertex_shader_instructions, st.num_pixel_shader_instructions))
    except Exception as e:
        unreal.log_warning("TAG precip statistics: %s" % e)


# M_TagCrowd2 (Phase 4, Research/NextLevel/03 A13 in shader form): the painted crowd (T_Crowd, the parameters of
# M_TagCrowd: TeamColor, Tiling, Glow, Roughness) where each fan in the texture (16 rows along U, ~55 seats along V a
# tile) shifts on his own beat: a little at rest, and three in four of them jumping with Hype (the crowd's excitement,
# set by Render/Stadium.cpp). Albedo darkens the painted fans under the lights. The sample keeps the unshifted UV's
# gradients, so the mip does not break along the fans' cell edges.
CROWD_HLSL = r"""
float2 uv = UV * Tiling.xy;
float2 cell = floor(float2(uv.x * 16.0, uv.y * 55.0));
float h = frac(sin(dot(cell, float2(12.9898, 78.233))) * 43758.5453);
float h2 = frac(h * 7.13 + 0.37);
float idle = 0.03 * sin(Tm * (0.7 + h2) + h * 40.0);
float jump = Hype * step(0.25, h2) * max(0.0, sin(Tm * (6.0 + 3.0 * h) + h * 6.2832)) * 0.22;
float4 c = Texture2DSampleGrad(Tex, TexSampler, uv + float2((idle + jump) / 16.0, 0.0), ddx(uv), ddy(uv));
return lerp(c.rgb, c.rgb * TeamColor, c.a) * Albedo;
"""


def build_crowd2():
    tex = unreal.load_asset("/Game/TAG/Textures/T_Crowd")
    if tex is None:
        unreal.log_warning("TAG crowd2: no /Game/TAG/Textures/T_Crowd (ue_build_materials.py): not built")
        return
    m = new_material("M_TagCrowd2")
    c = expr(m, unreal.MaterialExpressionCustom, -500, 0)
    c.set_editor_property("code", CROWD_HLSL)
    c.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    c.set_editor_property("description", "TAG crowd")
    ins = []
    for n in ("UV", "Tiling", "Tm", "Hype", "Tex", "TeamColor", "Albedo"):
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        ins.append(ci)
    c.set_editor_property("inputs", ins)
    MEL.connect_material_expressions(expr(m, unreal.MaterialExpressionTextureCoordinate, -900, 0), "", c, "UV")
    MEL.connect_material_expressions(vector(m, "Tiling", (8.0, 3.0, 0, 0), -900, 100), "", c, "Tiling")
    MEL.connect_material_expressions(expr(m, unreal.MaterialExpressionTime, -900, 200), "", c, "Tm")
    MEL.connect_material_expressions(scalar(m, "Hype", 0.0, -900, 300), "", c, "Hype")
    t = expr(m, unreal.MaterialExpressionTextureObjectParameter, -900, 400)
    t.set_editor_property("parameter_name", "Crowd")
    t.set_editor_property("texture", tex)
    MEL.connect_material_expressions(t, "", c, "Tex")
    MEL.connect_material_expressions(vector(m, "TeamColor", (0.1, 0.2, 0.8, 1), -900, 500), "", c, "TeamColor")
    MEL.connect_material_expressions(scalar(m, "Albedo", 1.0, -900, 600), "", c, "Albedo")
    MEL.connect_material_property(c, "", unreal.MaterialProperty.MP_BASE_COLOR)
    emis = op(m, unreal.MaterialExpressionMultiply, c, scalar(m, "Glow", 0.12, -300, 300), -100, 300)
    MEL.connect_material_property(emis, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(scalar(m, "Roughness", 0.95, -100, 450), "", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.recompile_material(m)
    EAL.save_loaded_asset(m)


# M_TagBreath (Phase 4, 03 A10): a breath puff, a soft sphere (Render/Stadium.cpp keeps a pool of them at the mouths
# of the men near the lens in cold games): lit translucency (the cheap volumetric mode), its edge fading out by the
# view angle, broken up by drifting noise; Puff is its strength as it swells and fades.
BREATH_HLSL = r"""
float f = saturate(dot(normalize(N), normalize(V)));
float n = Texture2DSample(Noise, NoiseSampler, P.xy / 40.0 + float2(Tm * 0.05, P.z / 60.0)).r;
return saturate(Puff * f * f * lerp(0.5, 1.3, n));
"""


def build_breath():
    noise = unreal.load_asset("/Game/TAG/Textures/T_TagTurfMacro")
    m = new_material("M_TagBreath")
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    tlm = getattr(unreal, "TranslucencyLightingMode", None)
    if tlm is not None and hasattr(tlm, "TLM_VOLUMETRIC_NON_DIRECTIONAL"):
        m.set_editor_property("translucency_lighting_mode", tlm.TLM_VOLUMETRIC_NON_DIRECTIONAL)
    try:
        m.set_editor_property("enable_responsive_aa", True)
    except Exception:
        pass
    c = expr(m, unreal.MaterialExpressionCustom, -500, 0)
    c.set_editor_property("code", BREATH_HLSL)
    c.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    c.set_editor_property("description", "TAG breath")
    ins = []
    for n in ("N", "V", "P", "Tm", "Puff", "Noise"):
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        ins.append(ci)
    c.set_editor_property("inputs", ins)
    MEL.connect_material_expressions(expr(m, unreal.MaterialExpressionVertexNormalWS, -900, 0), "", c, "N")
    MEL.connect_material_expressions(expr(m, unreal.MaterialExpressionCameraVectorWS, -900, 100), "", c, "V")
    MEL.connect_material_expressions(expr(m, unreal.MaterialExpressionWorldPosition, -900, 200), "", c, "P")
    MEL.connect_material_expressions(expr(m, unreal.MaterialExpressionTime, -900, 300), "", c, "Tm")
    MEL.connect_material_expressions(scalar(m, "Puff", 0.0, -900, 400), "", c, "Puff")
    t = expr(m, unreal.MaterialExpressionTextureObjectParameter, -900, 500)
    t.set_editor_property("parameter_name", "Noise")
    if noise is not None:
        t.set_editor_property("texture", noise)
        t.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    MEL.connect_material_expressions(t, "", c, "Noise")
    MEL.connect_material_property(c, "", unreal.MaterialProperty.MP_OPACITY)
    MEL.connect_material_property(vector(m, "Color", (0.82, 0.84, 0.88, 1), -300, 300), "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(scalar(m, "Roughness", 1.0, -300, 450), "", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.recompile_material(m)
    EAL.save_loaded_asset(m)


# TAG_STADIUM_MATS=precip builds only M_TagPrecip, =phase4 only M_TagCrowd2 and M_TagBreath (M_TagLED is deleted and
# made again on every full run)
only = os.environ.get("TAG_STADIUM_MATS", "")
if only == "":
    build_led()
if only in ("", "precip"):
    build_precip()
if only in ("", "phase4"):
    build_crowd2()
    build_breath()
unreal.log("TAG stadium materials built OK")
