"""Kit planner for the Madden 19 reference pipeline (Stage 6 generalisation of the Stage 4 prototype).

A kit is a JSON spec in tools/madden/kits/<name>.json. Every step is idempotent and writes job files
that madden.py runs. madden.py kit <spec> runs all steps in order with gates.

  python -I kit.py plan <spec>     jobs/<name>_plan.json   (MeshVariationDb EBX dumps and meshinfo per mesh)
  python -I kit.py jobs <spec>     jobs/<name>.json and jobs/ue_<name>.json, plus logs/kit_<name>_summary.json
                                   (skeleton bound check, projected size, excluded meshes)
  python -I kit.py matdesc <spec>  extracted/<out>/materials/<mesh>.material.json
  python -I kit.py mat <spec>      jobs/mat_<name>.json    (material instances and preview level)

Spec fields
  name, out, ue_dir                 identifiers and output locations
  skeleton, ue_skeleton             Madden skeleton EBX and the shared UE skeleton (null for static meshes)
  skeleton_bones                    bone count used for the bound check (268 for MaddenFB_Hero_Skeleton)
  mesh_type                         SkinnedMeshAsset (default) or RigidMeshAsset
  meshes                            explicit paths, "{b}" expands over body_types
  body_types                        list of suffix letters
  mesh_patterns                     regexes, first match each (or all matches when pattern_mode is "all")
  sample                            {"pattern": regex, "count": n, "seed": s} random sample from the inventory
  limit                             cap on the number of meshes after all selectors
  extra_textures                    {"dir": prefix, "regex": regex} team or kit textures to include
  overrides                         [[regex, {"BaseColor": leaf, "Normal": leaf, "RSM": leaf}], ...]
  preview                           {"mode": "grid", "spacing": 80} or {"mode": "bodytypes", ...}
"""
import csv
import json
import os
import random
import re
import sys

csv.field_size_limit(sys.maxsize)
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
INV = os.path.join(REPO, "extracted", "inventory", "inventory.csv")
JOBS = os.path.join(HERE, "jobs")
LOGS = os.path.join(REPO, "logs")
DEFAULT_TEX = re.compile(r"/(flatgrey_color|flatnormal_normal|invisible_color[^/]*|flatsweat)$", re.I)
PARAM_MAP = {"colorTexture": "BaseColor", "normalClampTexture": "Normal", "R_S_SSR_T_Texture": "RSM", "RSMTexture": "RSM",
             "diffuseTexture": "BaseColor", "normalTexture": "Normal", "colorMap": "BaseColor", "normalMap": "Normal",
             "nsmTexture": "NSM"}
WORLD_MASTER = "/Game/MaddenRef/Materials/M_MaddenRef_World"
MASTER = "/Game/MaddenRef/Materials/M_MaddenRef_Ref"
# Size projection per exported item. Observed in Stage 4: about 0.5 MB per LOD0 FBX and about 4 MB per texture
# (DDS plus PNG plus the converted normal). Doubled to stay on the safe side of the disk floor.
MESH_BYTES = 1 * 1024 ** 2
TEXTURE_BYTES = 8 * 1024 ** 2

_inv = None


def inventory():
    """path_lower -> (path, type, original_size). RES sizes are keyed as res:<path_lower>."""
    global _inv
    if _inv is None:
        _inv = {}
        with open(INV, encoding="utf-8") as fh:
            for r in csv.DictReader(fh):
                if r["kind"] == "ebx":
                    _inv[r["path"].lower()] = (r["path"], r["type"], int(r["original_size"] or 0))
                elif r["kind"] == "res":
                    _inv["res:" + r["path"].lower()] = (r["path"], r["type"], int(r["original_size"] or 0))
    return _inv


def leaf(path):
    return path.split("/")[-1]


def base_name(mesh):
    n = leaf(mesh)
    return n[:-len("_mesh")] if n.lower().endswith("_mesh") else n


def load_spec(path):
    s = json.load(open(path, encoding="utf-8"))
    s.setdefault("mesh_type", "SkinnedMeshAsset")
    s.setdefault("single_lod", True)
    s["OUT"] = "{REPO}/extracted/" + s["out"] + "/"
    s["OUTDIR"] = os.path.join(REPO, "extracted", s["out"]) + os.sep
    return s


def prefix(spec):
    return "SK_" if spec.get("skeleton") else "SM_"


def kit_meshes(spec):
    inv = inventory()
    mt = spec["mesh_type"]
    out = []

    def add(p):
        if p not in out:
            out.append(p)
    for m in spec.get("meshes", []):
        variants = [m.format(b=b) for b in spec.get("body_types", [""])] if "{b}" in m else [m]
        for v in variants:
            e = inv.get(v.lower())
            if e and e[1] == mt:
                add(e[0])
            else:
                print("missing mesh", v)
    for pat in spec.get("mesh_patterns", []):
        rx = re.compile(pat, re.I)
        hits = sorted(v[0] for k, v in inv.items() if v[1] == mt and not k.startswith("res:") and rx.search(k))
        if not hits:
            print("missing pattern", pat)
        for h in (hits if spec.get("pattern_mode") == "all" else hits[:1]):
            add(h)
    if spec.get("sample"):
        smp = spec["sample"]
        rx = re.compile(smp["pattern"], re.I)
        pool = sorted(v[0] for k, v in inv.items() if v[1] == mt and not k.startswith("res:") and rx.search(k))
        if smp.get("group"):
            # Sample whole groups (for example every mesh of one coach). The regex's first capture is the group.
            grx = re.compile(smp["group"], re.I)
            groups = sorted({grx.search(p.lower()).group(1) for p in pool if grx.search(p.lower())})
            random.Random(smp.get("seed", 19)).shuffle(groups)
            chosen = set(groups[:smp["count"]])
            for h in pool:
                g = grx.search(h.lower())
                if g and g.group(1) in chosen:
                    add(h)
        else:
            random.Random(smp.get("seed", 19)).shuffle(pool)
            for h in pool[:smp["count"]]:
                add(h)
    if spec.get("limit"):
        out = out[:spec["limit"]]
    return out


def mvdb_for(mesh):
    inv = inventory()
    base = mesh.lower()[:-len("_mesh")] + "_" if mesh.lower().endswith("_mesh") else mesh.lower() + "_"
    cands = [v[0] for k, v in inv.items() if v[1] == "MeshVariationDatabase" and k.startswith(base) and "_brt/" in k]
    # Prefer the DB whose folder adds the fewest name tokens after the mesh base. Otherwise a sibling part's
    # DB wins (for heads, <base>_hair_playerhead_brt sorts before <base>_playerhead_brt).
    def rank(c):
        rest = c.lower()[len(base):].split("/")[0]
        return (rest.count("_"), rest)
    cands.sort(key=rank)
    return cands[0] if cands else None


def dump_name(spec, path):
    return spec["OUT"] + "mvdb/" + path.replace("/", "__") + ".ebx.json"


def info_path(spec, mesh):
    return spec["OUTDIR"] + "meshinfo" + os.sep + leaf(mesh) + ".meshinfo.json"


def mesh_dump(spec, mesh):
    return spec["OUT"] + "meshebx/" + leaf(mesh) + ".ebx.json"


def preset_dump(spec, preset):
    return spec["OUT"] + "presets/" + preset.replace("/", "__") + ".ebx.json"


def cmd_plan(spec):
    jobs = []
    for m in kit_meshes(spec):
        mv = mvdb_for(m)
        if mv:
            jobs.append({"id": "mvdb_" + leaf(m), "op": "ebx", "asset": mv, "out": dump_name(spec, mv), "meta": {"mesh": m}})
        else:
            # World meshes keep materials in the mesh EBX (MeshMaterial -> SurfaceShaderPreset).
            jobs.append({"id": "mebx_" + leaf(m), "op": "ebx", "asset": m, "out": mesh_dump(spec, m)})
        jobs.append({"id": "mi_" + leaf(m), "op": "meshinfo", "asset": m, "out": spec["OUT"] + "meshinfo/" + leaf(m) + ".meshinfo.json"})
    json.dump(jobs, open(os.path.join(JOBS, spec["name"] + "_plan.json"), "w"), indent=1)
    print("plan jobs", len(jobs))


def _load(path):
    p = path.replace("{REPO}", REPO)
    return json.load(open(p, encoding="utf-8")) if os.path.exists(p) else None


def _mesh_materials(spec, mesh):
    d = _load(mesh_dump(spec, mesh))
    if not d:
        return []
    objs = d["objects"]
    mats = []
    for ref in objs[0].get("Materials", []) or []:
        if isinstance(ref, dict) and "ref" in ref and 0 <= ref["ref"] < len(objs):
            mats.append(objs[ref["ref"]])
    return mats


def cmd_plan2(spec):
    """Second plan pass for world meshes. Dumps the SurfaceShaderPresets their materials point at."""
    inv = inventory()
    jobs, seen = [], set()
    for m in kit_meshes(spec):
        if mvdb_for(m):
            continue
        for mat in _mesh_materials(spec, m):
            ext = ((mat.get("Shader") or {}).get("Shader") or {}).get("ext")
            if ext and ext.lower() in inv and ext.lower() not in seen:
                seen.add(ext.lower())
                real = inv[ext.lower()][0]
                jobs.append({"id": "preset_" + leaf(real), "op": "ebx", "asset": real, "out": preset_dump(spec, real)})
    json.dump(jobs, open(os.path.join(JOBS, spec["name"] + "_plan2.json"), "w"), indent=1)
    print("plan2 jobs", len(jobs))


def _vec(v):
    val = v.get("Value") or {}
    return [val.get(k) for k in ("x", "y", "z", "w")] if isinstance(val, dict) else val


def world_params(spec, mesh):
    """Material slots from the mesh EBX. Preset textures and vectors, overridden by the mesh material's own."""
    inv = inventory()
    out = []
    for slot, mat in enumerate(_mesh_materials(spec, mesh)):
        sh = mat.get("Shader") or {}
        ext = (sh.get("Shader") or {}).get("ext")
        tex, vec = {}, {}
        if ext and ext.lower() in inv:
            pd = _load(preset_dump(spec, inv[ext.lower()][0]))
            if pd:
                ps = pd["objects"][0].get("ShaderPreset") or {}
                for t in ps.get("TextureParameters", []) or []:
                    tex[t.get("ParameterName")] = (t.get("Value") or {}).get("ext")
                for v in ps.get("VectorParameters", []) or []:
                    vec[v.get("ParameterName")] = _vec(v)
        for t in sh.get("TextureParameters", []) or []:
            tex[t.get("ParameterName")] = (t.get("Value") or {}).get("ext")
        for v in sh.get("VectorParameters", []) or []:
            vec[v.get("ParameterName")] = _vec(v)
        out.append({"slot": slot, "preset": ext, "textures": tex, "vectors": vec})
    return out


def material_params(spec, mvdb, mesh):
    p = dump_name(spec, mvdb).replace("{REPO}", REPO)
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


def bound_check(spec, mesh):
    """Max vertex bone index across all LODs must be below the skeleton's bone count (Frosty maps Madden
    vertex bone indices straight onto the skeleton's bone order)."""
    if not spec.get("skeleton"):
        return True, None
    p = info_path(spec, mesh)
    if not os.path.exists(p):
        return False, "no meshinfo"
    d = json.load(open(p))
    mx = max([max(l["bone_index_array"]) for l in d["lods"] if l["bone_index_array"]] or [-1])
    return mx < spec.get("skeleton_bones", 268), mx


def cmd_jobs(spec):
    inv = inventory()
    meshes, excluded = [], []
    for m in kit_meshes(spec):
        ok, mx = bound_check(spec, m)
        if ok:
            meshes.append(m)
        else:
            excluded.append({"mesh": m, "max_bone_index": mx})
    jobs, textures, meta = [], {}, {}
    for m in meshes:
        mv = mvdb_for(m)
        params = material_params(spec, mv, m) if mv else world_params(spec, m)
        meta[m] = {"mvdb": mv, "materials": params}
        for slot in params or []:
            for name, t in slot["textures"].items():
                if name in PARAM_MAP and t and t.lower() in inv and inv[t.lower()][1] == "TextureAsset" and not DEFAULT_TEX.search(t.lower()):
                    textures.setdefault(inv[t.lower()][0], set()).add(leaf(m))
    if spec.get("extra_textures"):
        et = spec["extra_textures"]
        rx = re.compile(et["regex"], re.I)
        for k, v in inv.items():
            if k.startswith(et["dir"].lower()) and v[1] == "TextureAsset" and rx.search(k):
                textures.setdefault(v[0], set()).add("kit extra")
    projected = 0
    for m in meshes:
        j = {"id": "m_" + leaf(m), "op": "mesh", "asset": m, "out": spec["OUT"] + "meshes/" + leaf(m) + ".fbx",
             "units": "Meters", "single_lod": spec["single_lod"], "meta": meta[m]}
        if spec.get("skeleton"):
            j["skeleton"] = spec["skeleton"]
        jobs.append(j)
        projected += MESH_BYTES
    for t, users in sorted(textures.items()):
        jobs.append({"id": "t_" + leaf(t).lower(), "op": "texture", "asset": t, "out": spec["OUT"] + "textures/" + leaf(t).lower(),
                     "formats": ["dds", "png"], "meta": {"used_by": sorted(users)}})
        projected += TEXTURE_BYTES
    json.dump(jobs, open(os.path.join(JOBS, spec["name"] + ".json"), "w"), indent=1)
    ue = []
    if spec.get("ue_skeleton") and meshes:
        ue.append({"kind": "mesh", "file": spec["OUT"] + "meshes/" + leaf(meshes[0]) + ".fbx", "dest": spec["ue_skeleton"].rsplit("/", 1)[0],
                   "name": spec["ue_skeleton"].rsplit("/", 1)[1][:-len("_Skeleton")], "skeleton": spec["ue_skeleton"]})
    for m in meshes:
        j = {"kind": "mesh", "file": spec["OUT"] + "meshes/" + leaf(m) + ".fbx", "dest": spec["ue_dir"], "name": prefix(spec) + base_name(m)}
        if spec.get("ue_skeleton"):
            j["skeleton"] = spec["ue_skeleton"]
        ue.append(j)
    for t in sorted(textures):
        lt = leaf(t).lower()
        j = {"kind": "texture", "file": spec["OUT"] + "textures/" + lt + ".png", "dest": spec["ue_dir"] + "/Textures", "name": "T_" + lt}
        if lt.endswith("_nsm"):
            ue.append({"kind": "texture", "file": spec["OUT"] + "textures_ue/" + lt + "_n_ue.png", "dest": spec["ue_dir"] + "/Textures",
                       "name": "T_" + lt + "_n", "normal": True})
            ue.append({"kind": "texture", "file": spec["OUT"] + "textures_ue/" + lt + "_rsm_ue.png", "dest": spec["ue_dir"] + "/Textures",
                       "name": "T_" + lt + "_rsm", "srgb": False})
            continue
        if lt.endswith(("_norm", "_normal", "_nrm")) or "normal" in lt:
            j["normal"] = True
            j["file"] = spec["OUT"] + "textures_ue/" + lt + "_ue.png"
        elif "rsm" in lt or lt.endswith(("_mask", "_coeff")):
            j["srgb"] = False
        ue.append(j)
    json.dump(ue, open(os.path.join(JOBS, "ue_" + spec["name"] + ".json"), "w"), indent=1)
    summary = {"kit": spec["name"], "meshes": len(meshes), "textures": len(textures), "excluded": excluded,
               "projected_bytes": projected, "host_jobs": len(jobs), "ue_jobs": len(ue)}
    json.dump(summary, open(os.path.join(LOGS, "kit_%s_summary.json" % spec["name"]), "w"), indent=1)
    print(json.dumps({k: v for k, v in summary.items() if k != "excluded"}), "excluded", len(excluded))


def bindings_for(spec, mesh, meta):
    """Reference material instances. Texture parameter names come from the MeshVariationDb. Pieces whose
    team textures are chosen at runtime (uniforms, helmet shells) take the kit's explicit overrides."""
    ue_dir = spec["ue_dir"]
    name = base_name(mesh)
    for rx, o in spec.get("overrides", []):
        if re.search(rx, mesh.lower()):
            tex = {k: ue_dir + "/Textures/T_" + v for k, v in o.items()}
            return [{"mesh": ue_dir + "/" + prefix(spec) + name, "slot": "all", "mi": ue_dir + "/Materials/MI_" + name,
                     "textures": tex, "switches": {"UseRSM": "RSM" in tex, "RSMIsSkin": False}}]
    slots = meta.get("materials") or []
    p = info_path(spec, mesh)
    sections = json.load(open(p))["lods"][0]["sections"] if os.path.exists(p) else []
    by_slot = {s["slot"]: s for s in slots}
    # Frosty writes no FBX materials, so each imported mesh has one UE slot (known issue 5).
    # The LOD0 section with the most vertices supplies it, through its material id into the MeshVariationDb.
    out = []
    for sec in sorted(sections, key=lambda sec: -sec["vertex_count"])[:1]:
        s = by_slot.get(sec["material_id"])
        if s is None:
            continue
        tex = {}
        for pname, path in s["textures"].items():
            if pname in PARAM_MAP and path and not DEFAULT_TEX.search(path.lower()):
                key = PARAM_MAP[pname]
                t = ue_dir + "/Textures/T_" + leaf(path).lower()
                if key == "NSM":
                    tex.setdefault("Normal", t + "_n")
                    tex.setdefault("RSM", t + "_rsm")
                else:
                    tex.setdefault(key, t)
        if tex:
            b = {"mesh": ue_dir + "/" + prefix(spec) + name, "slot": 0, "mi": ue_dir + "/Materials/MI_" + name + "_" + sec["name"],
                 "textures": tex, "switches": {"UseRSM": "RSM" in tex, "RSMIsSkin": "R_S_SSR_T_Texture" in s["textures"]}}
            if not spec.get("skeleton"):
                tile = (s.get("vectors") or {}).get("colorTile")
                b["master"] = WORLD_MASTER
                b["scalars"] = {"Tiling": float(tile[0]) if isinstance(tile, list) and tile and tile[0] else 1.0}
            out.append(b)
    return out


def cmd_matdesc(spec):
    sys.path.insert(0, HERE)
    import madden
    jobs = json.load(open(os.path.join(JOBS, spec["name"] + ".json")))
    out_dir = spec["OUTDIR"] + "materials" + os.sep
    os.makedirs(out_dir, exist_ok=True)
    m = madden.load_manifest()
    n = 0
    for j in jobs:
        if j["op"] != "mesh":
            continue
        desc = {
            "source_mesh": j["asset"], "fbx": spec["out"] + "/meshes/" + leaf(j["asset"]) + ".fbx",
            "skeleton": spec.get("skeleton"), "mesh_variation_db": j["meta"].get("mvdb"),
            "madden_materials": j["meta"].get("materials"),
            "conventions": {"normal": "texfix.py: green flipped to DirectX, Z rebuilt from XY",
                            "RSMTexture": "R roughness, G specular, B metallic, A unknown (cloth B near 0)",
                            "R_S_SSR_T_Texture": "R roughness, G specular, B subsurface (not metallic), A translucency (skin)"},
            "unreal": {"mesh": spec["ue_dir"] + "/" + prefix(spec) + base_name(j["asset"]), "master": MASTER,
                       "bindings": bindings_for(spec, j["asset"], j["meta"])},
        }
        p = out_dir + leaf(j["asset"]) + ".material.json"
        text = json.dumps(desc, indent=1, sort_keys=True)
        if not os.path.exists(p) or open(p, encoding="utf-8").read() != text:
            open(p, "w", encoding="utf-8").write(text)
        m["files"][madden.rel(p)] = {"op": "matdesc", "stage": spec["name"], "source_asset": j["asset"], "size": os.path.getsize(p),
                                     "sha256": madden.sha256(p), "bundles": "", "superbundles": "", "params": {}}
        n += 1
    madden.save_manifest(m)
    print("material descriptions", n)


def cmd_mat(spec):
    jobs = json.load(open(os.path.join(JOBS, spec["name"] + ".json")))
    desc_dir = spec["OUTDIR"] + "materials" + os.sep
    ue_dir = spec["ue_dir"]
    binds = []
    meshes = [j["asset"] for j in jobs if j["op"] == "mesh"]
    for m in meshes:
        d = json.load(open(desc_dir + leaf(m) + ".material.json", encoding="utf-8"))
        binds.extend(d["unreal"]["bindings"])
    names = [prefix(spec) + base_name(m) for m in meshes]
    pv = spec.get("preview", {"mode": "grid", "spacing": 80})
    preview = []
    if pv["mode"] == "bodytypes":
        for b, x in pv["xs"].items():
            for n in names:
                if re.search(r"_%s$" % b, n):
                    preview.append({"mesh": ue_dir + "/" + n, "x": x, "label": b})
            if pv.get("head"):
                preview.append({"mesh": ue_dir + "/" + pv["head"], "x": x, "label": b})
            for n in names:
                if n.startswith(prefix(spec) + "helmet_") and pv["helmets"][b] in n:
                    preview.append({"mesh": ue_dir + "/" + n, "x": x, "label": b})
    elif pv["mode"] == "groups":
        grx = re.compile(pv["group_regex"], re.I)
        order = []
        for m, n in zip(meshes, names):
            g = grx.search(m.lower())
            key = g.group(1) if g else n
            if key not in order:
                order.append(key)
            preview.append({"mesh": ue_dir + "/" + n, "x": float(order.index(key) * pv.get("spacing", 100)), "label": key})
    else:
        per_row = pv.get("per_row", 20)
        sp = pv.get("spacing", 80)
        for i, n in enumerate(names):
            preview.append({"mesh": ue_dir + "/" + n, "x": float((i % per_row) * sp), "y": float((i // per_row) * sp), "label": "%03d" % i})
    out = {"bindings": binds, "preview_level": {"path": "/Game/MaddenRef/Maps/L_MaddenRef_" + pv.get("level", spec["name"].title().replace("_", "")), "meshes": preview}}
    json.dump(out, open(os.path.join(JOBS, "mat_" + spec["name"] + ".json"), "w"), indent=1)
    print("bindings", len(binds), "preview actors", len(preview))


if __name__ == "__main__":
    {"plan": cmd_plan, "plan2": cmd_plan2, "jobs": cmd_jobs, "matdesc": cmd_matdesc, "mat": cmd_mat}[sys.argv[1]](load_spec(sys.argv[2]))
