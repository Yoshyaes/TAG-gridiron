# Unreal Editor Python. Builds TAG Gridiron's own football gear meshes procedurally (LOOK-AND-FEEL-PLAN Phase 2).
#   UnrealEditor-Cmd.exe TAGGridiron.uproject -run=pythonscript -script="<this file>" -unattended -nosplash -nullrhi
#
# Original geometry from simple parametric shapes and real world gear proportions. No external or MaddenRef data.
# Output under /Game/TAG/Gear:
#   SM_TAG_Helmet          shell with face opening, ear holes, jaw flaps and a 1.2 cm wall. Slot 0 Shell, slot 1 Stripe.
#   SM_TAG_Facemask_Open   skill positions (QB, WR, DB, K/P)
#   SM_TAG_Facemask_Std    RB, LB, TE, FB
#   SM_TAG_Facemask_Cage   linemen
#   SM_TAG_ShoulderPads    yoke plus shoulder caps, in the spine_05 frame
#   SM_TAG_Football        28 x 17 cm ball along X with pointed tips; slot 0 Leather, slot 1 Laces
# Mesh frame matches the athlete "head" bone the gear attaches to: +X up, +Y forward (face), Z lateral. Units cm.
# The helmet origin is the shell centre; Athlete.cpp places it at head + (11, 1, 0).
import json
import math
import os

import unreal

OUT = "/Game/TAG/Gear"
REPORT = os.environ.get("TAG_GEAR_REPORT") or os.path.join(
    unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "gear_build.json")

# Shells are piecewise superellipsoids around the bone's up axis (x): forward (y) and lateral (z) semi axes, plus
# separate upper and lower vertical semi axes. Cells failing keep(f, s, u) are cut; a wall and rim make it solid.
HELMET = dict(c=(0.0, 0.0, 0.0), a_f=14.5, a_s=12.8, a_up=12.0, a_dn=22.0, exp=2.6, u_min=-13.5, wall=1.2,
              n_theta=96, n_phi=48)


def shell_point(sh, theta, u, inset=0.0):
    """theta 0 = forward. u relative to the shell centre. Returns (x_up, y_fwd, z_side) in mesh space."""
    a_u = sh["a_up"] if u >= 0 else sh["a_dn"]
    e = sh["exp"]
    k = max(0.0, 1.0 - abs(u / a_u) ** e) ** (1.0 / e)
    af, as_ = max(0.1, sh["a_f"] * k - inset), max(0.1, sh["a_s"] * k - inset)
    c, s = math.cos(theta), math.sin(theta)
    r = (abs(c) ** e + abs(s) ** e) ** (-1.0 / e)
    cx, cy, cz = sh["c"]
    return (cx + u - (inset * 0.6 if u > 0 else 0.0), cy + af * r * c, cz + as_ * r * s)


def helmet_keep(f, s, u):
    lower = -10.0
    if f > -2.0 and abs(s) > 7.5:
        lower = -13.5            # jaw flaps
    elif f < -6.0:
        lower = -11.0            # back flare
    if u < lower:
        return False
    if f > 2.5 and u < 1.5 and abs(s) < 9.5:
        return False             # face opening
    for side in (-1, 1):         # ear holes
        if (f - 0.5) ** 2 + (u + 4.5) ** 2 < 1.7 ** 2 and s * side > 9.0:
            return False
    return True


def helmet_slot(f, s, u):
    return 1 if (abs(s) < 1.6 and u > -1.0 and f < HELMET["a_f"] * 0.75) else 0


def build_shell(sh, keep, slot_of=lambda f, s, u: 0):
    """-> verts, uvs, tris [(a, b, c, slot)]. keep and slot_of take offsets from the shell centre."""
    nt, nph = sh["n_theta"], sh["n_phi"]
    us = [sh["a_up"] - (sh["a_up"] - sh["u_min"]) * j / nph for j in range(nph + 1)]
    ths = [2 * math.pi * i / nt for i in range(nt)]
    cx, cy, cz = sh["c"]
    verts, uvs = [], []
    idx = {}
    for layer, inset in ((0, 0.0), (1, sh["wall"])):
        for j, u in enumerate(us):
            for i, th in enumerate(ths):
                idx[(layer, i, j)] = len(verts)
                verts.append(shell_point(sh, th, u, inset))
                uvs.append((i / nt, j / nph))
    tris = []
    kept = set()
    for j in range(nph):
        for i in range(nt):
            th = 2 * math.pi * (i + 0.5) / nt
            u = 0.5 * (us[j] + us[j + 1])
            _, y, z = shell_point(sh, th, u)
            f, s = y - cy, z - cz
            if not keep(f, s, u):
                continue
            kept.add((i, j))
            slot = slot_of(f, s, u)
            i2 = (i + 1) % nt
            o = [idx[(0, i, j)], idx[(0, i2, j)], idx[(0, i2, j + 1)], idx[(0, i, j + 1)]]
            n = [idx[(1, i, j)], idx[(1, i2, j)], idx[(1, i2, j + 1)], idx[(1, i, j + 1)]]
            tris += [(o[0], o[1], o[2], slot), (o[0], o[2], o[3], slot)]
            tris += [(n[0], n[2], n[1], 0), (n[0], n[3], n[2], 0)]
    # rim: close every boundary edge between the outer and inner layers
    for (i, j) in kept:
        i2 = (i + 1) % nt
        im = (i - 1) % nt
        edges = [((i, j), (i2, j), (i, j - 1)),          # top edge, neighbour above
                 ((i2, j + 1), (i, j + 1), (i, j + 1)),  # bottom edge, neighbour below
                 ((i2, j), (i2, j + 1), (i2, j)),        # right edge, neighbour right
                 ((i, j + 1), (i, j), (im, j))]          # left edge, neighbour left
        for (a, b, nb) in edges:
            if nb in kept:
                continue
            oa, ob = idx[(0, a[0], a[1])], idx[(0, b[0], b[1])]
            ia, ib = idx[(1, a[0], a[1])], idx[(1, b[0], b[1])]
            tris += [(oa, ia, ib, 0), (oa, ib, ob, 0)]
    return verts, uvs, tris


# Shoulder pads in the spine_05 frame (x up the spine, y forward/back, z lateral), origin at the bone.
# A yoke arching over chest and upper back with a neck opening and open armpits, plus a cap over each shoulder.
# Athlete.cpp scales y and z per body type.
PAD_YOKE = dict(c=(4.0, 0.0, 0.0), a_f=15.5, a_s=23.0, a_up=10.0, a_dn=16.0, exp=2.3, u_min=-12.0, wall=1.6,
                n_theta=96, n_phi=40)
PAD_CAP = dict(a_f=12.5, a_s=10.5, a_up=5.5, a_dn=7.0, exp=2.2, u_min=-2.5, wall=1.4, n_theta=64, n_phi=20)


def yoke_keep(f, s, u):
    if u > 1.0 and (f / 10.5) ** 2 + (s / 9.5) ** 2 < 1.0:
        return False            # neck opening
    if abs(s) > 18.0 and u < 0.0:
        return False            # armpits open under the caps
    return True


def merge(*parts):
    verts, uvs, tris = [], [], []
    for v, uv, t in parts:
        base = len(verts)
        verts += v
        uvs += uv
        tris += [(a + base, b + base, c + base, sl) for a, b, c, sl in t]
    return verts, uvs, tris


def build_pads():
    parts = [build_shell(PAD_YOKE, yoke_keep)]
    for side in (-1, 1):
        cap = dict(PAD_CAP, c=(9.5, 0.0, 17.5 * side))
        parts.append(build_shell(cap, lambda f, s, u: True))
    return merge(*parts)


# Facemask. Bars are arcs in front of the face opening; vertical bars run down the front.
CF, RF, RS = 3.5, 13.5, 10.6
TUBE_R, TUBE_SIDES = 0.48, 8


def arc(u, t0, t1, n=20):
    return [(u, CF + RF * math.cos(t), RS * math.sin(t)) for t in (t0 + (t1 - t0) * k / n for k in range(n + 1))]


def vbar(t, u0, u1, n=10):
    pts = []
    for k in range(n + 1):
        u = u0 + (u1 - u0) * k / n
        bulge = 0.8 * math.sin(math.pi * k / n)
        pts.append((u, CF + RF * math.cos(t) + bulge, RS * math.sin(t)))
    return pts


def mask_style(style):
    d = math.radians
    if style == "Open":
        return [arc(-3.0, d(-80), d(80)), arc(-9.5, d(-70), d(70)), arc(-12.8, d(-45), d(45), 12),
                vbar(0.0, -3.0, -12.8)]
    if style == "Std":
        return [arc(-3.0, d(-80), d(80)), arc(-6.8, d(-75), d(75)), arc(-10.2, d(-68), d(68)), arc(-13.0, d(-48), d(48), 12),
                vbar(0.0, -3.0, -13.0), vbar(d(32), -3.0, -11.5), vbar(d(-32), -3.0, -11.5)]
    return [arc(-1.0, d(-35), d(35), 10), arc(-3.6, d(-82), d(82)), arc(-6.6, d(-78), d(78)), arc(-9.6, d(-72), d(72)),
            arc(-12.8, d(-52), d(52), 12),
            vbar(0.0, -1.0, -12.8), vbar(d(24), -1.0, -12.0), vbar(d(-24), -1.0, -12.0),
            vbar(d(50), -3.6, -10.5), vbar(d(-50), -3.6, -10.5)]


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def norm(a):
    l = math.sqrt(a[0] ** 2 + a[1] ** 2 + a[2] ** 2) or 1.0
    return (a[0] / l, a[1] / l, a[2] / l)


def build_tubes(paths):
    verts, uvs, tris = [], [], []
    for pts in paths:
        base = len(verts)
        for k, p in enumerate(pts):
            t = norm(sub(pts[min(k + 1, len(pts) - 1)], pts[max(k - 1, 0)]))
            ref = (0.0, 1.0, 0.0) if abs(t[1]) < 0.9 else (1.0, 0.0, 0.0)
            n1 = norm(cross(t, ref))
            n2 = cross(t, n1)
            for r in range(TUBE_SIDES):
                a = 2 * math.pi * r / TUBE_SIDES
                c, s = math.cos(a) * TUBE_R, math.sin(a) * TUBE_R
                verts.append((p[0] + n1[0] * c + n2[0] * s, p[1] + n1[1] * c + n2[1] * s, p[2] + n1[2] * c + n2[2] * s))
                uvs.append((r / TUBE_SIDES, k / max(1, len(pts) - 1)))
        for k in range(len(pts) - 1):
            for r in range(TUBE_SIDES):
                r2 = (r + 1) % TUBE_SIDES
                a, b = base + k * TUBE_SIDES + r, base + k * TUBE_SIDES + r2
                c, dd = base + (k + 1) * TUBE_SIDES + r2, base + (k + 1) * TUBE_SIDES + r
                tris += [(a, b, c, 0), (a, c, dd, 0)]
        for end, sign in ((0, 1), (len(pts) - 1, -1)):   # caps
            ring = [base + end * TUBE_SIDES + r for r in range(TUBE_SIDES)]
            for r in range(1, TUBE_SIDES - 1):
                tri = (ring[0], ring[r], ring[r + 1]) if sign > 0 else (ring[0], ring[r + 1], ring[r])
                tris.append(tri + (0,))
    return verts, uvs, tris


def build_football(half_len=14.0, radius=8.5, nx=48, nt=36):
    """Regulation-proportioned ball (28 cm long, 17 cm across) along X: a surface of revolution whose profile
    r(x) = R (1 - (x/L)^2)^0.82 gives the pointed tips; a raised lace strip on top is slot 1 (Laces)."""
    verts, uvs, tris = [], [], []
    xs = [-half_len + 2 * half_len * i / nx for i in range(nx + 1)]
    ring = []
    for i, x in enumerate(xs):
        r = radius * max(0.0, 1.0 - (x / half_len) ** 2) ** 0.82
        row = []
        for j in range(nt):
            th = 2 * math.pi * j / nt
            lace = abs(math.atan2(math.sin(th), math.cos(th))) < math.radians(9) and abs(x) < 4.6
            rr = r * (1.025 if lace else 1.0)
            row.append(len(verts))
            verts.append((x, rr * math.sin(th), rr * math.cos(th)))     # theta 0 = +Z (top)
            uvs.append((i / nx, j / nt))
        ring.append(row)
    for i in range(nx):
        for j in range(nt):
            j2 = (j + 1) % nt
            a, b, c, d = ring[i][j], ring[i][j2], ring[i + 1][j2], ring[i + 1][j]
            xm = 0.5 * (xs[i] + xs[i + 1])
            thm = 2 * math.pi * (j + 0.5) / nt
            slot = 1 if (abs(math.atan2(math.sin(thm), math.cos(thm))) < math.radians(9) and abs(xm) < 4.6) else 0
            tris += [(a, b, c, slot), (a, c, d, slot)]
    return verts, uvs, tris


def make_mesh(name, verts, uvs, tris, slots):
    path = OUT + "/" + name
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    sm = unreal.EditorAssetLibrary.duplicate_asset("/Engine/BasicShapes/Cube", path)
    desc = unreal.StaticMesh.create_static_mesh_description(sm)
    groups = [desc.create_polygon_group() for _ in slots]
    for g, sname in zip(groups, slots):
        desc.set_polygon_group_material_slot_name(g, sname)
    vids = []
    for v in verts:
        vid = desc.create_vertex()
        desc.set_vertex_position(vid, unreal.Vector(v[0], v[1], v[2]))
        vids.append(vid)
    for a, b, c, slot in tris:
        vis = []
        for k in (a, b, c):
            vi = desc.create_vertex_instance(vids[k])
            desc.set_vertex_instance_uv(vi, unreal.Vector2D(uvs[k][0], uvs[k][1]), 0)
            vis.append(vi)
        desc.create_triangle(groups[slot], vis)
    sm.set_editor_property("static_materials", [unreal.StaticMaterial(material_interface=None, material_slot_name=s) for s in slots])
    # Full build (fast_build=False) commits the description as the source model, so normals, tangents, Lumen cards
    # and distance fields are derived properly. The fast path left invalid derived data (renderer precision ensure).
    sm.build_from_static_mesh_descriptions([desc], False, False)
    sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem) or unreal.EditorStaticMeshLibrary
    bs = sub.get_lod_build_settings(sm, 0)
    bs.set_editor_property("recompute_normals", True)
    bs.set_editor_property("recompute_tangents", True)
    bs.set_editor_property("use_mikk_t_space", True)
    bs.set_editor_property("remove_degenerates", True)
    sub.set_lod_build_settings(sm, 0, bs)   # rebuilds LOD 0 from the committed description
    unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
    b = sm.get_bounding_box()
    return {"asset": path, "verts": len(verts), "tris": len(tris),
            "bounds_min": [round(b.min.x, 2), round(b.min.y, 2), round(b.min.z, 2)],
            "bounds_max": [round(b.max.x, 2), round(b.max.y, 2), round(b.max.z, 2)]}


def main():
    report = {"meshes": [], "errors": []}
    try:
        v, uv, t = build_shell(HELMET, helmet_keep, helmet_slot)
        report["meshes"].append(make_mesh("SM_TAG_Helmet", v, uv, t, ["Shell", "Stripe"]))
        v, uv, t = build_pads()
        report["meshes"].append(make_mesh("SM_TAG_ShoulderPads", v, uv, t, ["Pad"]))
        v, uv, t = build_football()
        report["meshes"].append(make_mesh("SM_TAG_Football", v, uv, t, ["Leather", "Laces"]))
        for style in ("Open", "Std", "Cage"):
            v, uv, t = build_tubes(mask_style(style))
            report["meshes"].append(make_mesh("SM_TAG_Facemask_" + style, v, uv, t, ["Mask"]))
    except Exception as e:
        import traceback
        report["errors"].append(traceback.format_exc())
    json.dump(report, open(REPORT, "w"), indent=1)
    unreal.log("TAG_GEAR " + ("ok" if not report["errors"] else "FAILED") + " " + json.dumps(report)[:2000])


main()
