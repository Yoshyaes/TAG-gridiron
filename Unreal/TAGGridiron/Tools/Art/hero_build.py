"""Blender worker: builds TAG character meshes from the model pack's part FBX files (D:\\MaddenRef\\ModelPack).

  blender -b --factory-startup --python Tools/Art/hero_build.py -- <jobs.json> [<results.jsonl>]

Each job joins its parts on one armature, puts every section in the material slot the job names, renames the pack's
hero / crowd rig bones to the Unreal names the game code and the retargeters use (Hips -> pelvis, LeftArm ->
upperarm_l, ...), drops the trajectory helpers (root -> pelvis directly) and writes <out>.fbx plus <out>.json (slots,
their pack textures, measurements of the bind pose). Jobs come from hero_jobs.py. Finished jobs are skipped unless
"force" is set. Personal use only, like the pack itself: everything this writes lives in the git-ignored RawArt/Hero.
"""
import json
import os
import sys

import bpy

# pack rig name -> Unreal name. Same names on the hero (268 bones) and crowd (34 bones) rigs.
RENAME = {"Reference": "root", "Hips": "pelvis", "Spine": "spine_01", "Spine1": "spine_03", "Spine2": "spine_04",
          "Spine3": "spine_05", "Neck": "neck_01", "Neck1": "neck_02", "Head": "head"}
for S, s in (("Left", "l"), ("Right", "r")):
    RENAME.update({S + "Shoulder": "clavicle_" + s, S + "Arm": "upperarm_" + s, S + "ForeArm": "lowerarm_" + s,
                   S + "Hand": "hand_" + s, S + "UpLeg": "thigh_" + s, S + "Leg": "calf_" + s, S + "Foot": "foot_" + s,
                   S + "ToeBase": "ball_" + s, S + "HandThumb1": "thumb_01_" + s, S + "HandThumb2": "thumb_02_" + s,
                   S + "HandThumb3": "thumb_03_" + s})
    for f, u in (("Index", "index"), ("Middle", "middle"), ("Ring", "ring"), ("Pinky", "pinky")):
        RENAME[S + "InHand" + f] = u + "_metacarpal_" + s
        for k in (1, 2, 3):
            RENAME["%sHand%s%d" % (S, f, k)] = "%s_%02d_%s" % (u, k, s)
# helpers the game never animates: the trajectory chain sits between the root and the hips in the pack rigs
DROP = ("AITrajectory", "Trajectory", "TrajectoryEnd", "Focus", "FocusEnd", "LeftAnkleEffectorAux", "RightAnkleEffectorAux")
HIDE = ("shadow",)


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def slot_material(name, cache):
    if name not in cache:
        cache[name] = bpy.data.materials.new(name)
    return cache[name]


def build(job):
    reset()
    cache = {}
    root_arm = None
    meshes = []
    slots = {}
    for part in job["parts"]:
        before = set(bpy.data.objects)
        bpy.ops.import_scene.fbx(filepath=part["fbx"])
        new = [o for o in bpy.data.objects if o not in before]
        arm = next((o for o in new if o.type == "ARMATURE"), None)
        for o in new:
            if o.type != "MESH":
                continue
            sec = o.name.split(".")[0]
            low = sec.lower() + " " + os.path.basename(part["fbx"]).lower()
            slot = part["slots"].get(sec) or part["slots"].get("*")
            if any(h in low for h in HIDE) or not slot:
                bpy.data.objects.remove(o, do_unlink=True)
                continue
            o.data.materials.clear()
            o.data.materials.append(slot_material(slot, cache))
            info = slots.setdefault(slot, {"sections": [], "textures": part.get("textures", {}).get(sec, {})})
            info["sections"].append(os.path.basename(part["fbx"])[:-4] + ":" + sec)
            if part.get("uv_scale"):
                n = part["uv_scale"]
                for layer in o.data.uv_layers[:1]:
                    for d in layer.data:
                        d.uv = (d.uv[0] / n, d.uv[1] / n + 1.0 - 1.0 / n)
            # one UV set everywhere (Unreal merges sections by UV count; the pack's extra sets tile detail maps). A
            # part can name the set its colour texture uses (helmet shells: set 1, the stripe / decal layout)
            # A job with "uv_sets": 2 keeps a second set (Unreal TexCoord 1) on every part: the part's "uv2" set (helmet
            # shells: set 3, where the pack's vent / hardware detail normal lies), else a copy of the first, so every
            # section has the same count.
            k = part.get("uv", 0)
            k2 = part.get("uv2", k)
            second = None
            if job.get("uv_sets") == 2:
                srcl = o.data.uv_layers[k2] if 0 <= k2 < len(o.data.uv_layers) else o.data.uv_layers[0]
                second = [tuple(d.uv) for d in srcl.data]
            if 0 < k < len(o.data.uv_layers):
                src, dst = o.data.uv_layers[k], o.data.uv_layers[0]
                for i in range(len(dst.data)):
                    dst.data[i].uv = src.data[i].uv
            while len(o.data.uv_layers) > 1:
                o.data.uv_layers.remove(o.data.uv_layers[-1])
            o.data.uv_layers[0].name = "UVMap"
            if second is not None:
                l2 = o.data.uv_layers.new(name="UVDetail")
                for i, uv in enumerate(second):
                    l2.data[i].uv = uv
            meshes.append(o)
            if root_arm is not None and arm is not None:
                o.parent = root_arm
                for mod in o.modifiers:
                    if mod.type == "ARMATURE":
                        mod.object = root_arm
        if root_arm is None:
            root_arm = arm
        elif arm is not None:
            bpy.data.objects.remove(arm, do_unlink=True)
        for o in new:
            try:
                if o.type == "EMPTY":
                    bpy.data.objects.remove(o, do_unlink=True)
            except ReferenceError:
                pass
    if not meshes or root_arm is None:
        raise RuntimeError("no meshes or no armature")

    # one mesh object
    bpy.ops.object.select_all(action="DESELECT")
    for o in meshes:
        o.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    if len(meshes) > 1:
        bpy.ops.object.join()
    mesh = bpy.context.view_layer.objects.active
    mesh.name = job["name"]
    mesh.data.name = job["name"]

    # skin repair: a pack mesh whose weights point at the wrong bones takes them from an intact sibling (the same
    # outfit's other variant, the same fan type), surface to surface
    if job.get("weights_from"):
        before = set(bpy.data.objects)
        for f in job["weights_from"]:
            bpy.ops.import_scene.fbx(filepath=f)
        new = [o for o in bpy.data.objects if o not in before]
        donors = [o for o in new if o.type == "MESH"]
        for o in new:
            if o.type != "MESH":
                o.select_set(False)
        if donors:
            bpy.ops.object.select_all(action="DESELECT")
            for o in donors:
                o.select_set(True)
            bpy.context.view_layer.objects.active = donors[0]
            if len(donors) > 1:
                bpy.ops.object.join()
            donor = bpy.context.view_layer.objects.active
            for g in donor.vertex_groups:
                if mesh.vertex_groups.get(g.name) is None:
                    mesh.vertex_groups.new(name=g.name)
            bpy.ops.object.select_all(action="DESELECT")
            mesh.select_set(True)
            bpy.context.view_layer.objects.active = mesh
            dt = mesh.modifiers.new("WeightsFrom", "DATA_TRANSFER")
            dt.object = donor
            dt.use_vert_data = True
            dt.data_types_verts = {"VGROUP_WEIGHTS"}
            dt.vert_mapping = "POLYINTERP_NEAREST"
            dt.layers_vgroup_select_src = "ALL"
            dt.layers_vgroup_select_dst = "NAME"
            dt.mix_mode = "REPLACE"
            bpy.ops.object.modifier_move_to_index(modifier=dt.name, index=0)
            bpy.ops.object.modifier_apply(modifier=dt.name)
        for o in new:
            try:
                bpy.data.objects.remove(o, do_unlink=True)
            except ReferenceError:
                pass

    # torn sleeves with no intact twin to copy from (Reporter1's casual top, both variants: 649 of 719 sleeve vertices
    # weighted to the spine, 1,180 chest vertices on bones > 30 cm away): vertices of the named slots whose strongest
    # bone lies > 30 cm away are re-skinned to the two nearest upper-body bones (distance to the bone segments,
    # inverse-square blend)
    rs = job.get("reskin_far")
    if rs:
        import re
        Mw = root_arm.matrix_world
        # targets: the whole upper body (a torn top hangs chest vertices on the forearms too, not only the sleeves)
        pat = re.compile(rs.get("bones", r"^(Hips|Spine\d?|Neck1?|(Left|Right)(Shoulder|Arm|ForeArm|Hand))$|^RM_(Left|Right)(Arm|ForeArm)Twist\d$"))
        segs = {b.name: (Mw @ b.head_local, Mw @ b.tail_local) for b in root_arm.data.bones if pat.match(b.name)}
        ends = {b.name: (Mw @ b.head_local, Mw @ b.tail_local) for b in root_arm.data.bones}
        mids = {i for i, m in enumerate(mesh.data.materials) if m and m.name.split(".")[0] in rs["slots"]}
        verts = set()
        for p in mesh.data.polygons:
            if p.material_index in mids:
                verts.update(p.vertices)
        gname = {g.index: g.name for g in mesh.vertex_groups}

        def seg_dist(pt, a, b):
            ab = b - a
            t = max(0.0, min(1.0, (pt - a).dot(ab) / max(ab.length_squared, 1e-12)))
            return (pt - (a + ab * t)).length

        fixed = 0
        for vi in verts:
            v = mesh.data.vertices[vi]
            if not v.groups:
                continue
            g = max(v.groups, key=lambda x: x.weight)
            hb = ends.get(gname.get(g.group))
            pw = mesh.matrix_world @ v.co
            if hb is None or min((pw - hb[0]).length, (pw - hb[1]).length) <= rs.get("far", 0.3):
                continue
            near = sorted((seg_dist(pw, a, b), n) for n, (a, b) in segs.items())[:2]
            # only where an arm bone is nearer than the bone it hangs on now (the chest stays on the spine)
            if near[0][0] >= seg_dist(pw, hb[0], hb[1]):
                continue
            w = [1.0 / max(d, 0.005) ** 2 for d, _ in near]
            for old in [x.group for x in v.groups]:
                mesh.vertex_groups[old].remove([vi])
            for (d, n), wi in zip(near, w):
                vg = mesh.vertex_groups.get(n) or mesh.vertex_groups.new(name=n)
                vg.add([vi], wi / sum(w), "REPLACE")
            fixed += 1
        print("HERO_RESKIN %s %d vertices" % (job["name"], fixed), flush=True)

    # a hand prop skinned to one bone and reaching far from it (the chain crew's rods): its vertices are dropped
    sp = job.get("strip_prop")
    if sp:
        import re
        import bmesh
        Mw = root_arm.matrix_world
        heads = {b.name: Mw @ b.head_local for b in root_arm.data.bones if re.search(sp["bones"], b.name)}
        gname = {g.index: g.name for g in mesh.vertex_groups}
        kill = []
        for v in mesh.data.vertices:
            if not v.groups:
                continue
            g = max(v.groups, key=lambda x: x.weight)
            h = heads.get(gname.get(g.group))
            if h is not None and ((mesh.matrix_world @ v.co) - h).length > sp.get("far", 0.14):
                kill.append(v.index)
        bm = bmesh.new()
        bm.from_mesh(mesh.data)
        bm.verts.ensure_lookup_table()
        bmesh.ops.delete(bm, geom=[bm.verts[i] for i in kill], context="VERTS")
        bm.to_mesh(mesh.data)
        bm.free()
        print("HERO_STRIP_PROP %s %d vertices (bones %s)" % (job["name"], len(kill), sorted(heads)), flush=True)

    # bind pose measurements (Blender space: metres, character faces -Y) before the renames
    arm = root_arm
    M = arm.matrix_world
    pos = {b.name: list(M @ b.head_local) for b in arm.data.bones}
    # skin sanity: share of vertices whose strongest bone is more than 60 cm away (a pack mesh whose weights point at
    # the wrong bones tears apart once animated; props held in a hand score high by design)
    heads = {b.name: (M @ b.head_local, M @ b.tail_local) for b in arm.data.bones}
    gn = {g.index: g.name for g in mesh.vertex_groups}
    far = cnt = 0
    for v in mesh.data.vertices:
        if v.groups:
            g = max(v.groups, key=lambda x: x.weight)
            hb = heads.get(gn.get(g.group))
            if hb:
                pw = mesh.matrix_world @ v.co
                cnt += 1
                far += min((pw - hb[0]).length, (pw - hb[1]).length) > 0.6
    far_share = round(far / max(cnt, 1), 3)

    # drop the trajectory helpers, rename to Unreal names
    used = {g.name for g in mesh.vertex_groups}
    bpy.ops.object.select_all(action="DESELECT")
    arm.select_set(True)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.mode_set(mode="EDIT")
    eb = arm.data.edit_bones
    if "Hips" in eb and "Reference" in eb:
        eb["Hips"].parent = eb["Reference"]
    for n in DROP:
        if n in eb and n not in used:
            eb.remove(eb[n])
    bpy.ops.object.mode_set(mode="OBJECT")
    for b in arm.data.bones:
        if b.name in RENAME:
            old = b.name
            b.name = RENAME[old]
            g = mesh.vertex_groups.get(old)
            if g is not None:
                g.name = RENAME[old]
    arm.name = "Armature"
    arm.data.name = "Armature"
    if job.get("collapse"):
        # physics proxy: every weight moves to its nearest named (animated) ancestor, so the physics asset Unreal
        # generates from this mesh has bodies on the main bones only (no twist, pad or face helpers)
        keep = set(RENAME.values())
        bones = arm.data.bones
        target = {}
        for b in bones:
            p = b
            while p is not None and p.name not in keep:
                p = p.parent
            target[b.name] = p.name if p is not None else "pelvis"
        groups = {g.index: g.name for g in mesh.vertex_groups}
        for n in set(target.values()):
            if mesh.vertex_groups.get(n) is None:
                mesh.vertex_groups.new(name=n)
        acc = {}
        for v in mesh.data.vertices:
            w = {}
            for g in v.groups:
                t = target.get(groups[g.group], groups[g.group])
                w[t] = w.get(t, 0.0) + g.weight
            acc[v.index] = w
        for g in list(mesh.vertex_groups):
            if g.name not in keep:
                mesh.vertex_groups.remove(g)
            else:
                g.remove(range(len(mesh.data.vertices)))
        for vi, w in acc.items():
            for n, x in w.items():
                mesh.vertex_groups[n].add([vi], x, "REPLACE")

    # Unreal layout: a root bone at the origin above the pelvis, centimetres, no scale on any node. The pack's top bone
    # (Reference) comes into Blender as the armature object, so the hips were the root bone and the metre -> cm
    # factor of the export landed on them as a 100x scale (children in metres); the retargeted pose then put every
    # child 100x too far from the hips. Bake it instead: Blender unit scale 0.01, armature and mesh x100, applied.
    import mathutils
    scene = bpy.context.scene
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.scale_length = 0.01
    arm.matrix_world = mathutils.Matrix.Scale(100.0, 4) @ arm.matrix_world
    bpy.context.view_layer.update()
    for o in (arm, mesh):  # parent first: the mesh keeps its place, then its own (now 100x) transform is baked
        bpy.ops.object.select_all(action="DESELECT")
        o.select_set(True)
        bpy.context.view_layer.objects.active = o
        bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    bpy.ops.object.select_all(action="DESELECT")
    arm.select_set(True)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.mode_set(mode="EDIT")
    eb = arm.data.edit_bones
    if "root" not in eb:
        root = eb.new("root")
        root.head = (0.0, 0.0, 0.0)
        root.tail = (0.0, -20.0, 0.0)
        for b in eb:
            if b.parent is None and b.name != "root":
                b.parent = root
                b.use_connect = False
    bpy.ops.object.mode_set(mode="OBJECT")

    # bounds of the joined mesh (world space)
    lo = mathutils.Vector((1e9, 1e9, 1e9))
    hi = -lo
    for v in mesh.data.vertices:
        w = mesh.matrix_world @ v.co
        lo = mathutils.Vector(map(min, lo, w))
        hi = mathutils.Vector(map(max, hi, w))

    os.makedirs(os.path.dirname(job["out"]), exist_ok=True)
    bpy.ops.object.select_all(action="DESELECT")
    arm.select_set(True)
    mesh.select_set(True)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.export_scene.fbx(filepath=job["out"], use_selection=True, object_types={"ARMATURE", "MESH"},
                             add_leaf_bones=False, armature_nodetype="NULL", apply_unit_scale=True, global_scale=1.0,
                             mesh_smooth_type="FACE", use_mesh_modifiers=False, bake_anim=False,
                             use_armature_deform_only=False, use_tspace=False)
    side = {"name": job["name"], "kind": job.get("kind"), "rig": job.get("rig", "hero"), "slots": slots,
            "vertices": len(mesh.data.vertices), "triangles": sum(len(p.vertices) - 2 for p in mesh.data.polygons),
            "bounds_cm": [list(lo), list(hi)], "far": far_share, "bones": len(arm.data.bones), "meta": job.get("meta", {}),
            "bind_m": {RENAME.get(k, k): v for k, v in pos.items() if k in RENAME}}
    json.dump(side, open(job["out"][:-4] + ".json", "w"), indent=1)
    return {"vertices": side["vertices"], "triangles": side["triangles"], "bones": side["bones"]}


def main():
    args = sys.argv[sys.argv.index("--") + 1:]
    jobs = json.load(open(args[0]))
    res = open(args[1], "a", encoding="utf-8") if len(args) > 1 else None
    for job in jobs:
        if os.path.exists(job["out"]) and os.path.exists(job["out"][:-4] + ".json") and not job.get("force"):
            continue
        try:
            r = dict(build(job), name=job["name"], status="ok")
        except Exception as e:  # keep going, the driver reports failures
            import traceback
            r = {"name": job["name"], "status": "fail", "error": repr(e), "trace": traceback.format_exc()[-800:]}
        line = json.dumps(r)
        print("HERO_BUILD", line, flush=True)
        if res:
            res.write(line + "\n")
            res.flush()


main()
