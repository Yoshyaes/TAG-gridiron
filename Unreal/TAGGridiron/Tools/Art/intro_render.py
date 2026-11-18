# Blender (headless): the TAG GRIDIRON brand sting, frame by frame (Tools/Art/build_intro.py runs it and encodes
# Content/Movies/TAG_Intro.mp4, the game's startup movie).
#   "TAG" in gold lands from close up as the camera settles to a front view; a light sweeps across the letters; the
#   yard-line underline draws itself and "GRIDIRON" rises into place below. 150 frames at 30 fps, 1920x1080, Eevee.
#   blender -b -P intro_render.py -- <out_dir> [frames]
import math
import os
import sys

import bpy

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = argv[0] if argv else os.path.join(os.getcwd(), "intro_frames")
FRAMES = int(argv[1]) if len(argv) > 1 else 150
os.makedirs(OUT, exist_ok=True)

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.frame_start, scene.frame_end = 1, FRAMES
scene.render.fps = 30
scene.render.resolution_x, scene.render.resolution_y = 1920, 1080
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = "PNG"
scene.render.image_settings.color_mode = "RGB"
scene.render.filepath = os.path.join(OUT, "frame_")
scene.render.film_transparent = False
for eng in ("BLENDER_EEVEE_NEXT", "BLENDER_EEVEE"):
    try:
        scene.render.engine = eng
        break
    except TypeError:
        continue
ev = scene.eevee
for attr, val in (("taa_render_samples", 64), ("use_raytracing", True), ("use_shadows", True), ("use_volumetric_shadows", True),
                  ("volumetric_tile_size", "4"), ("shadow_ray_count", 2), ("shadow_step_count", 4)):
    try:
        setattr(ev, attr, val)
    except Exception:
        pass
try:
    scene.view_settings.view_transform = "AgX"
    scene.view_settings.look = "AgX - Medium High Contrast"
except Exception:
    pass

FONT_HEAD = next((f for f in (r"C:\Windows\Fonts\impact.ttf", r"C:\Windows\Fonts\ariblk.ttf", r"C:\Windows\Fonts\arialbd.ttf") if os.path.exists(f)), None)
FONT_SUB = next((f for f in (r"C:\Windows\Fonts\bahnschrift.ttf", r"C:\Windows\Fonts\seguibl.ttf", r"C:\Windows\Fonts\arialbd.ttf") if os.path.exists(f)), None)


def fcurves_of(act):
    """An action's F-curves (Blender 5 keeps them in layered channel bags; older builds on the action)."""
    if not act:
        return []
    if getattr(act, "fcurves", None) is not None:
        return list(act.fcurves)
    out = []
    for layer in act.layers:
        for strip in layer.strips:
            for cb in strip.channelbags:
                out.extend(cb.fcurves)
    return out


def ease(obj, path, frames_values, interp="BEZIER", easing="EASE_OUT", index=-1):
    for f, v in frames_values:
        if index >= 0:
            getattr(obj, path)[index] = v
            obj.keyframe_insert(path, index=index, frame=f)
        else:
            setattr(obj, path, v)
            obj.keyframe_insert(path, frame=f)
    ad = obj.animation_data if hasattr(obj, "animation_data") else None
    act = ad.action if ad else None
    for fc in fcurves_of(act):
        for kp in fc.keyframe_points:
            kp.interpolation = interp
            kp.easing = easing


def metal(name, color, rough, emit=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    bsdf = m.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = (*color, 1.0)
    bsdf.inputs["Metallic"].default_value = 0.85
    bsdf.inputs["Roughness"].default_value = rough
    try:
        bsdf.inputs["Coat Weight"].default_value = 0.4
        bsdf.inputs["Coat Roughness"].default_value = 0.08
    except KeyError:
        pass
    bsdf.inputs["Emission Color"].default_value = (*color, 1.0)
    bsdf.inputs["Emission Strength"].default_value = emit
    return m, bsdf


# ---- the words
bpy.ops.object.text_add(location=(0, 0, 0.05))
head = bpy.context.object
head.name = "TAG"
hd = head.data
hd.body = "TAG"
if FONT_HEAD:
    hd.font = bpy.data.fonts.load(FONT_HEAD)
hd.size = 2.6
hd.extrude = 0.16
hd.bevel_depth = 0.028
hd.bevel_resolution = 4
hd.align_x = "CENTER"
hd.align_y = "CENTER"
hd.space_character = 0.96
head.rotation_euler = (math.radians(90), 0, 0)
gold, gold_bsdf = metal("Gold", (1.0, 0.58, 0.05), 0.32)
head.data.materials.append(gold)
# lands: big and a touch turned, overshooting to rest at frame 46; the gold flashes as it lands
ease(head, "scale", [(1, (1.7, 1.7, 1.7)), (34, (0.97, 0.97, 0.97)), (46, (1.0, 1.0, 1.0))])
ease(head, "rotation_euler", [(1, (math.radians(90), math.radians(-7), math.radians(-14))), (40, (math.radians(90), 0, 0))])
gold_bsdf.inputs["Emission Strength"].default_value = 0.0
gold_bsdf.inputs["Emission Strength"].keyframe_insert("default_value", frame=30)
gold_bsdf.inputs["Emission Strength"].default_value = 0.3
gold_bsdf.inputs["Emission Strength"].keyframe_insert("default_value", frame=37)
gold_bsdf.inputs["Emission Strength"].default_value = 0.0
gold_bsdf.inputs["Emission Strength"].keyframe_insert("default_value", frame=52)

bpy.ops.object.text_add(location=(0, 0, -1.55))
sub = bpy.context.object
sub.name = "GRIDIRON"
sd = sub.data
sd.body = "GRIDIRON"
if FONT_SUB:
    sd.font = bpy.data.fonts.load(FONT_SUB)
sd.size = 0.62
sd.extrude = 0.03
sd.bevel_depth = 0.006
sd.align_x = "CENTER"
sd.align_y = "CENTER"
sd.space_character = 1.32
sub.rotation_euler = (math.radians(90), 0, 0)
steel = bpy.data.materials.new("Steel")
steel.use_nodes = True
nt = steel.node_tree
sb = nt.nodes["Principled BSDF"]
sb.inputs["Base Color"].default_value = (0.86, 0.88, 0.92, 1)
sb.inputs["Metallic"].default_value = 0.9
sb.inputs["Roughness"].default_value = 0.28
out = nt.nodes["Material Output"]
mix = nt.nodes.new("ShaderNodeMixShader")
transp = nt.nodes.new("ShaderNodeBsdfTransparent")
nt.links.new(transp.outputs[0], mix.inputs[1])
nt.links.new(sb.outputs[0], mix.inputs[2])
nt.links.new(mix.outputs[0], out.inputs["Surface"])
for attr, val in (("surface_render_method", "BLENDED"), ("blend_method", "BLEND")):
    try:
        setattr(steel, attr, val)
    except Exception:
        pass
mix.inputs[0].default_value = 0.0
mix.inputs[0].keyframe_insert("default_value", frame=44)
mix.inputs[0].default_value = 1.0
mix.inputs[0].keyframe_insert("default_value", frame=68)
sub.data.materials.append(steel)
ease(sub, "location", [(44, (0, 0, -1.9)), (72, (0, 0, -1.42))])

# the yard line under the word: draws from the centre outward as the sub-title arrives
bpy.ops.mesh.primitive_cube_add(location=(0, 0.0, -0.98))
line = bpy.context.object
line.name = "Line"
line.scale = (0.001, 0.025, 0.012)
lm, lb = metal("LineGold", (1.0, 0.78, 0.15), 0.3, emit=3.0)
line.data.materials.append(lm)
ease(line, "scale", [(46, (0.001, 0.025, 0.012)), (74, (3.1, 0.025, 0.012))], index=-1)

# ---- the stage: a dark glossy floor for the reflections, dark fog for the sweep's shafts
bpy.ops.mesh.primitive_plane_add(size=60, location=(0, 0, -1.95))
floor = bpy.context.object
fm = bpy.data.materials.new("Floor")
fm.use_nodes = True
fb = fm.node_tree.nodes["Principled BSDF"]
fb.inputs["Base Color"].default_value = (0.012, 0.014, 0.022, 1)
fb.inputs["Metallic"].default_value = 0.6
fb.inputs["Roughness"].default_value = 0.18
floor.data.materials.append(fm)
world = bpy.data.worlds.new("World")
scene.world = world
world.use_nodes = True
wn = world.node_tree
bg = wn.nodes["Background"]
bg.inputs[0].default_value = (0.0015, 0.002, 0.005, 1)
bg.inputs[1].default_value = 1.0
vol = wn.nodes.new("ShaderNodeVolumeScatter")
vol.inputs["Density"].default_value = 0.004
vol.inputs["Anisotropy"].default_value = 0.7
wn.links.new(vol.outputs[0], wn.nodes["World Output"].inputs["Volume"])

# ---- stadium light banks far behind the words (soft discs through the shallow focus)
bank_mat = bpy.data.materials.new("Bank")
bank_mat.use_nodes = True
bb = bank_mat.node_tree.nodes["Principled BSDF"]
bb.inputs["Emission Color"].default_value = (1.0, 0.95, 0.85, 1)
bb.inputs["Emission Strength"].default_value = 60.0
import random
random.seed(7)
for bx in (-17.0, -6.0, 6.0, 17.0):
    for r in range(2):
        for c in range(4):
            bpy.ops.mesh.primitive_uv_sphere_add(radius=0.28, location=(bx + (c - 1.5) * 1.1, 31.0, 6.2 + r * 1.1), segments=12, ring_count=8)
            bpy.context.object.data.materials.append(bank_mat)

# ---- lights
def light(name, kind, loc, energy, color=(1, 1, 1), size=None, spot=None):
    ld = bpy.data.lights.new(name, kind)
    ld.energy = energy
    ld.color = color
    if size is not None and hasattr(ld, "size"):
        ld.size = size
    if spot and kind == "SPOT":
        ld.spot_size, ld.spot_blend = spot
    lo = bpy.data.objects.new(name, ld)
    scene.collection.objects.link(lo)
    lo.location = loc
    return lo


def aim(obj, target):
    c = obj.constraints.new("TRACK_TO")
    c.target = target
    c.track_axis = "TRACK_NEGATIVE_Z"
    c.up_axis = "UP_Y"


focus = bpy.data.objects.new("Focus", None)
scene.collection.objects.link(focus)
focus.location = (0, 0, -0.35)
key = light("Key", "AREA", (3.5, -5.5, 3.5), 1500, (1.0, 0.93, 0.82), size=3.0)
aim(key, focus)
rim = light("Rim", "AREA", (-4.0, 4.5, 2.5), 1400, (0.55, 0.7, 1.0), size=4.0)
aim(rim, focus)
fill = light("Fill", "AREA", (-5.0, -6.0, -0.5), 350, (0.8, 0.85, 1.0), size=6.0)
aim(fill, focus)
sweep = light("Sweep", "SPOT", (-7.0, -4.0, 2.2), 9000, (1.0, 0.97, 0.9), spot=(math.radians(28), 0.5))
sweep_aim = bpy.data.objects.new("SweepAim", None)
scene.collection.objects.link(sweep_aim)
sweep_aim.location = (-4.5, 0, 0.0)
aim(sweep, sweep_aim)
# the sweep crosses the letters left to right as they land, and again more softly for the sub-title
ease(sweep, "location", [(28, (-7.0, -4.0, 2.2)), (78, (7.0, -4.0, 2.2))], easing="EASE_IN_OUT")
ease(sweep_aim, "location", [(28, (-4.5, 0, 0.0)), (78, (4.5, 0, 0.0))], easing="EASE_IN_OUT")
sweep.data.energy = 0.0
sweep.data.keyframe_insert("energy", frame=24)
sweep.data.energy = 9000.0
sweep.data.keyframe_insert("energy", frame=34)
sweep.data.keyframe_insert("energy", frame=72)
sweep.data.energy = 0.0
sweep.data.keyframe_insert("energy", frame=84)

# ---- camera: low and close from the left, settling to a level front view
cam_d = bpy.data.cameras.new("Cam")
cam_d.lens = 40
cam_d.dof.use_dof = True
cam_d.dof.focus_object = focus
cam_d.dof.aperture_fstop = 0.9
cam = bpy.data.objects.new("Cam", cam_d)
scene.collection.objects.link(cam)
scene.camera = cam
cam_aim = bpy.data.objects.new("CamAim", None)
scene.collection.objects.link(cam_aim)
aim(cam, cam_aim)
ease(cam, "location", [(1, (-3.6, -7.2, -1.3)), (60, (-0.7, -8.7, -0.2)), (FRAMES, (0.0, -9.3, 0.1))])
ease(cam_aim, "location", [(1, (-0.9, 0, -0.2)), (60, (-0.15, 0, -0.5)), (FRAMES, (0.0, 0, -0.6))])

# ---- a glow on the highlights (the gold flash, the sweep's glint)
def compositor_glare():
    """A bloom on the highlights (the gold flash, the sweep's glint). Blender 5 keeps the compositor as a node group
    on the scene with menu sockets on the glare node; older builds use scene.node_tree with enum properties."""
    tree = None
    if hasattr(scene, "compositing_node_group"):
        tree = bpy.data.node_groups.new("Comp", "CompositorNodeTree")
        tree.interface.new_socket("Image", in_out="INPUT", socket_type="NodeSocketColor")
        tree.interface.new_socket("Image", in_out="OUTPUT", socket_type="NodeSocketColor")
        gi = tree.nodes.new("NodeGroupInput")
        go = tree.nodes.new("NodeGroupOutput")
        src, dst = gi.outputs[0], go.inputs[0]
    else:
        scene.use_nodes = True
        tree = scene.node_tree
        for n in list(tree.nodes):
            tree.nodes.remove(n)
        rl = tree.nodes.new("CompositorNodeRLayers")
        comp = tree.nodes.new("CompositorNodeComposite")
        src, dst = rl.outputs["Image"], comp.inputs["Image"]
    glare = tree.nodes.new("CompositorNodeGlare")
    if "Type" in glare.inputs:
        glare.inputs["Type"].default_value = "Bloom"
        glare.inputs["Quality"].default_value = "High"
        glare.inputs["Threshold"].default_value = 1.1
        glare.inputs["Strength"].default_value = 0.45
        glare.inputs["Size"].default_value = 0.55
    else:
        glare.glare_type = "BLOOM" if "BLOOM" in glare.bl_rna.properties["glare_type"].enum_items else "FOG_GLOW"
        glare.threshold = 1.1
        glare.mix = 0.0
    tree.links.new(src, glare.inputs["Image"])
    tree.links.new(glare.outputs[0], dst)
    if hasattr(scene, "compositing_node_group"):
        scene.compositing_node_group = tree


try:
    if os.environ.get("TAG_INTRO_NOCOMP"):
        raise RuntimeError("disabled")
    compositor_glare()
except Exception as e:
    print("TAG_INTRO compositor glare skipped:", e)
    try:
        scene.compositing_node_group = None
    except Exception:
        pass

PREVIEW = len(argv) > 2 and argv[2] == "preview"
print("TAG_INTRO rendering", "preview stills" if PREVIEW else FRAMES, "frames with", scene.render.engine, "->", OUT)
if PREVIEW:
    for f in (1, 24, 38, 56, 80, 140):
        scene.frame_set(f)
        scene.render.filepath = os.path.join(OUT, "preview_%03d.png" % f)
        bpy.ops.render.render(write_still=True)
else:
    bpy.ops.render.render(animation=True)
print("TAG_INTRO done")
