# Unreal Editor Python (headless). Per-item check of the pack details the hero materials carry, every catalog item
# against its own build sidecar (RawArt/Hero/<Kind>/<name>.json):
#   Helmets: the Shell slot samples its normal on TexCoord 1 (NormalUV1 = 1), that normal is the shell's own pack normal
#            map, and the mesh holds two UV channels (the detail set hero_build.py keeps).
#   Kits:    a cleat whose pack part has brand marks (logoOverlayTexture) draws that very decal (DecalOn = 1, the
#            texture imported from that file); a cleat without marks draws none.
# Prints one TAG_ITEM line per failure and a summary; report Saved/hero_item_check.json.
import hashlib
import json
import os
import re

import unreal

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
PROJ = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
RAW = os.path.join(PROJ, "RawArt", "Hero")
cat = json.load(open(os.path.join(PROJ, "Content", "TAG", "Hero", "hero_catalog.json")))


def tex_name(src):  # hero_import.py's texture asset name for a pack file
    base = re.sub(r"[^A-Za-z0-9_]", "_", os.path.splitext(os.path.basename(src))[0])
    return "T_" + base[:60] + "_" + hashlib.md5(src.lower().replace("\\", "/").encode()).hexdigest()[:5]


def uv_channels(mesh):
    try:
        return unreal.SkeletalMeshEditorSubsystem().get_num_uv_channels(mesh, 0) if hasattr(unreal.SkeletalMeshEditorSubsystem, "get_num_uv_channels") else None
    except Exception:
        return None


rep = {"helmets": {"checked": 0, "fail": []}, "kits": {"checked": 0, "with_decal": 0, "fail": []}}
for it in cat["items"]:
    kind, name = it["kind"], it["name"]
    if kind not in ("Helmet", "Kit"):
        continue
    side = json.load(open(os.path.join(RAW, kind, name + ".json")))
    mesh = unreal.load_asset(it["path"].split(".")[0])
    slots = {str(s.get_editor_property("material_slot_name")): s.get_editor_property("material_interface") for s in mesh.get_editor_property("materials")}
    if kind == "Helmet":
        rep["helmets"]["checked"] += 1
        mi = slots.get("Shell")
        want = (side["slots"].get("Shell", {}).get("textures", {}) or {}).get("normalClampTexture")
        got = MEL.get_material_instance_texture_parameter_value(mi, "Normal") if mi else None
        nuv = MEL.get_material_instance_scalar_parameter_value(mi, "NormalUV1") if mi else 0.0
        uvs = uv_channels(mesh)
        why = []
        if not mi:
            why.append("no Shell slot")
        if nuv < 0.5:
            why.append("NormalUV1 %.1f" % nuv)
        if want and (not got or got.get_name() != tex_name(want) + "_N"):
            why.append("normal %s, want %s" % (got.get_name() if got else None, tex_name(want) + "_N"))
        if uvs is not None and uvs < 2:
            why.append("%d UV channels" % uvs)
        if why:
            rep["helmets"]["fail"].append({"item": name, "why": why})
            unreal.log("TAG_ITEM helmet %s: %s" % (name, "; ".join(why)))
        rep["helmets"]["uv_channels_checked"] = uvs is not None
    else:
        rep["kits"]["checked"] += 1
        mi = slots.get("Shoes")
        want = (side["slots"].get("Shoes", {}).get("textures", {}) or {}).get("logoOverlayTexture")
        on = MEL.get_material_instance_scalar_parameter_value(mi, "DecalOn") if mi else 0.0
        got = MEL.get_material_instance_texture_parameter_value(mi, "Decal") if mi else None
        why = []
        if want:
            rep["kits"]["with_decal"] += 1
            if on < 0.5:
                why.append("DecalOn %.1f" % on)
            if not got or got.get_name() != tex_name(want):
                why.append("decal %s, want %s" % (got.get_name() if got else None, tex_name(want)))
        elif on > 0.5:
            why.append("a decal on a cleat without marks")
        if why:
            rep["kits"]["fail"].append({"item": name, "why": why})
            unreal.log("TAG_ITEM kit %s: %s" % (name, "; ".join(why)))
json.dump(rep, open(os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "hero_item_check.json"), "w"), indent=1)
unreal.log("TAG_ITEM summary: helmets %d checked, %d fail (UV channels read: %s); kits %d checked, %d with pack decals, %d fail" % (
    rep["helmets"]["checked"], len(rep["helmets"]["fail"]), rep["helmets"].get("uv_channels_checked"), rep["kits"]["checked"], rep["kits"]["with_decal"], len(rep["kits"]["fail"])))
