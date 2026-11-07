"""Builds the Stage 4 prototype job files from a kit spec and the Stage 1 inventory.

Two passes, both idempotent.
  python -I prototype.py mvdb   writes jobs/prototype_mvdb.json (EBX dumps of each mesh's MeshVariationDb)
  python -I prototype.py jobs   reads those dumps, writes jobs/prototype.json (meshes, textures, meta)
                                and jobs/ue_prototype.json for Unreal
  python -I prototype.py mat    writes jobs/mat_prototype.json (material instances and preview level)

The kit (Fred's prototype set, animations cut after Stage 3)
  one skeleton, three body types (t thin, m muscular, h heavy), two helmets (Xenith, Riddell SpeedFlex),
  the uniform pieces to dress them (49ers 2018 WHIALT jersey and pants, GOLALT helmet), one head.
"""
import csv
import json
import os
import re
import sys

csv.field_size_limit(sys.maxsize)
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
INV = os.path.join(REPO, "extracted", "inventory", "inventory.csv")
OUT = "{REPO}/extracted/prototype/"
SKELETON = "content/characters/rig/skeleton/player/MaddenFB_Hero_Skeleton"
UE_SKEL = "/Game/MaddenRef/Skeleton/MaddenFB_Hero_Skeleton"
UE_DIR = "/Game/MaddenRef/Players/Prototype"
BODY_TYPES = ["t", "m", "h"]
P = "content/characters/player/"
PER_BODY = [
    P + "bodies/fullarms_{b}_mesh",
    P + "bodies/legs_{b}_mesh",
    P + "parts/torsoapparel/jersey/jersey_{b}_mesh",
    P + "parts/legsapparel/pants/pants_{b}_mesh",
    P + "parts/legsapparel/socks_mid/socks_mid_{b}_mesh",
    P + "parts/torsoapparel/sleeve_tight/sleeve_tight_{b}_mesh",
    P + "parts/footwear/shoe_low_nikealphamenaceelite99club/shoe_low_nikealphamenaceelite99clubr_{b}_mesh",
    P + "parts/footwear/shoe_low_nikealphamenaceelite99club/shoe_low_nikealphamenaceelite99clubl_{b}_mesh",
    P + "parts/armgear/glover/glover_{b}_mesh",
    P + "parts/armgear/glovel/glovel_{b}_mesh",
]
SHARED_PATTERNS = [
    r"^content/characters/player/players/f/faulkmarshall_11413/faulkmarshall_11413_mesh$",
    r"^content/characters/player/parts/helmets/xenith/helmet_shell_xenith_mesh$",
    r"^content/characters/player/parts/helmets/xenith/facemasks/helmet_facemask_xenithprism_mesh$",
    r"^content/characters/player/parts/helmets/speedflex/helmet_shell_speedflex_mesh$",
    r"^content/characters/player/parts/helmets/speedflex/facemasks/helmet_facemask_speedflex[^/]*_mesh$",
]
UNIFORM_DIR = "content/characters/player/parts/uniforms/49ers/2018/"
UNIFORM_RX = re.compile(r"(WHIALT_(Jersey|Pants)_(COL|NORM|RSM)|GOLALT_(helmet|Sticker)_COL|socksUnder_COL)$", re.I)


def load_inventory():
    ebx = {}
    with open(INV, encoding="utf-8") as fh:
        for r in csv.DictReader(fh):
            if r["kind"] == "ebx":
                ebx[r["path"].lower()] = (r["path"], r["type"])
    return ebx


def kit_meshes(ebx):
    want = [p.format(b=b) for b in BODY_TYPES for p in PER_BODY]
    meshes = []
    for w in want:
        if w.lower() in ebx and ebx[w.lower()][1] == "SkinnedMeshAsset":
            meshes.append(ebx[w.lower()][0])
        else:
            print("missing mesh", w)
    for pat in SHARED_PATTERNS:
        rx = re.compile(pat)
        hits = sorted(v[0] for k, v in ebx.items() if v[1] == "SkinnedMeshAsset" and rx.search(k))
        if not hits:
            print("missing pattern", pat)
        meshes.extend(hits[:1])
    return meshes


def mvdb_for(mesh, ebx):
    base = mesh.lower()[:-len("_mesh")] + "_"
    cands = sorted(v[0] for k, v in ebx.items() if v[1] == "MeshVariationDatabase" and k.startswith(base) and "_brt/" in k)
    return cands[0] if cands else None


def leaf(path):
    return path.split("/")[-1]


def dump_name(path):
    return OUT + "mvdb/" + path.replace("/", "__") + ".ebx.json"


def cmd_mvdb():
    ebx = load_inventory()
    jobs = []
    for m in kit_meshes(ebx):
        mv = mvdb_for(m, ebx)
        if mv:
            jobs.append({"id": "mvdb_" + leaf(m), "op": "ebx", "asset": mv, "out": dump_name(mv), "meta": {"mesh": m}})
        jobs.append({"id": "mi_" + leaf(m), "op": "meshinfo", "asset": m, "out": OUT + "meshinfo/" + leaf(m) + ".meshinfo.json"})
    json.dump(jobs, open(os.path.join(HERE, "jobs", "prototype_mvdb.json"), "w"), indent=1)
    print("mvdb jobs", len(jobs))


def material_params(dump_path, mesh):
    p = dump_path.replace("{REPO}", REPO)
    if not os.path.exists(p):
        return None
    d = json.load(open(p, encoding="utf-8"))
    out = []
    for obj in d["objects"]:
        for e in obj.get("Entries", []) or []:
            if not isinstance(e.get("Mesh"), dict) or e["Mesh"].get("ext", "").lower() != mesh.lower():
                continue
            for slot, mat in enumerate(e.get("Materials", []) or []):
                tex = {t.get("ParameterName"): (t.get("Value") or {}).get("ext") for t in mat.get("TextureParameters", []) or []}
                vec = {}
                for v in mat.get("VectorParameters", []) or []:
                    val = v.get("Value")
                    if isinstance(val, dict):
                        val = [val.get(k) for k in ("x", "y", "z", "w")]
                    vec[v.get("ParameterName")] = val
                out.append({"slot": slot, "textures": tex, "vectors": vec})
    return out


def cmd_jobs():
    ebx = load_inventory()
    meshes = kit_meshes(ebx)
    jobs, textures = [], {}
    meta_by_mesh = {}
    for m in meshes:
        mv = mvdb_for(m, ebx)
        params = material_params(dump_name(mv), m) if mv else None
        meta_by_mesh[m] = {"mvdb": mv, "materials": params}
        for slot in params or []:
            for name, t in slot["textures"].items():
                if t and t.lower() in ebx and ebx[t.lower()][1] == "TextureAsset":
                    textures.setdefault(ebx[t.lower()][0], set()).add(leaf(m))
    for k, v in ebx.items():
        if k.startswith(UNIFORM_DIR.lower()) and v[1] == "TextureAsset" and UNIFORM_RX.search(k):
            textures.setdefault(v[0], set()).add("uniform 49ers 2018")
    for m in meshes:
        jobs.append({"id": "m_" + leaf(m), "op": "mesh", "asset": m, "out": OUT + "meshes/" + leaf(m) + ".fbx",
                     "units": "Meters", "skeleton": SKELETON, "single_lod": True, "meta": meta_by_mesh[m]})
    for t, users in sorted(textures.items()):
        jobs.append({"id": "t_" + leaf(t).lower(), "op": "texture", "asset": t, "out": OUT + "textures/" + leaf(t).lower(),
                     "formats": ["dds", "png"], "meta": {"used_by": sorted(users)}})
    json.dump(jobs, open(os.path.join(HERE, "jobs", "prototype.json"), "w"), indent=1)
    ue = [{"kind": "mesh", "file": OUT + "meshes/" + leaf(meshes[0]) + ".fbx", "dest": "/Game/MaddenRef/Skeleton",
           "name": "MaddenFB_Hero", "skeleton": UE_SKEL}]
    for m in meshes:
        ue.append({"kind": "mesh", "file": OUT + "meshes/" + leaf(m) + ".fbx", "dest": UE_DIR,
                   "name": "SK_" + leaf(m)[:-len("_mesh")], "skeleton": UE_SKEL})
    for t in sorted(textures):
        lt = leaf(t).lower()
        j = {"kind": "texture", "file": OUT + "textures/" + lt + ".png", "dest": UE_DIR + "/Textures", "name": "T_" + lt}
        if lt.endswith(("_norm", "_normal")) or "normal" in lt:
            j["normal"] = True
        elif "rsm" in lt or lt.endswith(("_mask", "_coeff")):
            j["srgb"] = False
        ue.append(j)
    json.dump(ue, open(os.path.join(HERE, "jobs", "ue_prototype.json"), "w"), indent=1)
    print("prototype jobs", len(jobs), "meshes", len(meshes), "textures", len(textures), "ue jobs", len(ue))


DEFAULT_TEX = re.compile(r"/(flatgrey_color|flatnormal_normal|invisible_color[^/]*|flatsweat)$", re.I)
PARAM_MAP = {"colorTexture": "BaseColor", "normalClampTexture": "Normal", "R_S_SSR_T_Texture": "RSM", "RSMTexture": "RSM"}
UNIFORM_OVERRIDES = [
    (re.compile(r"/jersey_[tmhd]_mesh$"), {"BaseColor": "nin_jersey_2018_whialt_jersey_col", "Normal": "nin_jersey_2018_whialt_jersey_norm", "RSM": "nin_jersey_2018_whialt_jersey_rsm"}),
    (re.compile(r"/pants_[tmhd]_mesh$"), {"BaseColor": "nin_pants_2018_whialt_pants_col", "Normal": "nin_pants_2018_whialt_pants_norm", "RSM": "nin_pants_2018_whialt_pants_rsm"}),
    (re.compile(r"/helmet_shell_[a-z0-9]+_mesh$"), {"BaseColor": "nin_helmet_2018_golalt_helmet_col"}),
]


def bindings_for(mesh, meta):
    """Reference material instances per slot. Texture parameter names come from the MeshVariationDb.
    Uniform pieces only carry template textures there (team textures are chosen at runtime), so the
    49ers 2018 set is applied as an override."""
    name = leaf(mesh)[:-len("_mesh")]
    out = []
    override = next((o for rx, o in UNIFORM_OVERRIDES if rx.search(mesh.lower())), None)
    slots = meta.get("materials") or []
    if override is not None:
        tex = {k: UE_DIR + "/Textures/T_" + v for k, v in override.items()}
        return [{"mesh": UE_DIR + "/SK_" + name, "slot": "all", "mi": UE_DIR + "/Materials/MI_" + name,
                 "textures": tex, "switches": {"UseRSM": "RSM" in tex}}]
    # MeshVariationDb slots index materials across every LOD. The export keeps LOD0 only, and Interchange
    # makes one UE slot per LOD0 section in order, so UE slot i takes the MVDB slot of section i's material id.
    info_path = OUT.replace("{REPO}", REPO) + "meshinfo/" + leaf(mesh) + ".meshinfo.json"
    sections = json.load(open(info_path))["lods"][0]["sections"] if os.path.exists(info_path) else []
    by_slot = {s["slot"]: s for s in slots}
    # Frosty writes no FBX materials (EXP_FBX_MATERIAL is off in its exporter), so every imported mesh has
    # a single UE slot. The section with the most vertices supplies that slot's material.
    sections = sorted(sections, key=lambda sec: -sec["vertex_count"])[:1]
    for ue_slot, sec in enumerate(sections):
        s = by_slot.get(sec["material_id"])
        if s is None:
            continue
        tex = {}
        for pname, path in s["textures"].items():
            if pname in PARAM_MAP and path and not DEFAULT_TEX.search(path.lower()):
                tex[PARAM_MAP[pname]] = UE_DIR + "/Textures/T_" + leaf(path).lower()
        if not tex:
            continue
        out.append({"mesh": UE_DIR + "/SK_" + name, "slot": ue_slot, "mi": UE_DIR + "/Materials/MI_" + name + "_" + sec["name"],
                    "textures": tex, "switches": {"UseRSM": "RSM" in tex}})
    return out


def cmd_mat():
    jobs = json.load(open(os.path.join(HERE, "jobs", "prototype.json")))
    binds, preview = [], []
    xs = {"t": -120.0, "m": 0.0, "h": 120.0}
    helmets = {"t": "xenith", "m": "speedflex", "h": "xenith"}
    for j in jobs:
        if j["op"] != "mesh":
            continue
        binds.extend(bindings_for(j["asset"], j["meta"]))
    names = ["SK_" + leaf(j["asset"])[:-len("_mesh")] for j in jobs if j["op"] == "mesh"]
    for b, x in xs.items():
        for n in names:
            if re.search(r"_%s$" % b, n):
                preview.append({"mesh": UE_DIR + "/" + n, "x": x, "label": b})
        preview.append({"mesh": UE_DIR + "/SK_faulkmarshall_11413", "x": x, "label": b})
        h = helmets[b]
        for n in names:
            if n.startswith("SK_helmet_") and h in n:
                preview.append({"mesh": UE_DIR + "/" + n, "x": x, "label": b})
    spec = {"bindings": binds, "preview_level": {"path": "/Game/MaddenRef/Maps/L_MaddenRef_Prototype", "meshes": preview}}
    json.dump(spec, open(os.path.join(HERE, "jobs", "mat_prototype.json"), "w"), indent=1)
    print("bindings", len(binds), "preview actors", len(preview))


if __name__ == "__main__":
    {"mvdb": cmd_mvdb, "jobs": cmd_jobs, "mat": cmd_mat}[sys.argv[1]]()
