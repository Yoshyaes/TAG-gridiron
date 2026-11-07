"""Animation kits (Checkpoints D and E). Called by madden.py kit for specs with "kind": "anim".

  py -3.13 -I animkit.py plan <spec>     resolve controller names to body and finger clips, write
                                          jobs/<name>_anim.json, jobs/ue_<name>.json, logs/kit_<name>_summary.json
  py -3.13 -I animkit.py export <spec>   decode, plausibility gate, FBX per clip, manifest entries,
                                          logs/kit_<name>_checks.json (failed clips are left out of the UE jobs)
  py -3.13 -I animkit.py mat <spec>      jobs/mat_<name>.json, a preview level with the dressed Stage 2 player

Spec fields
  name, kind "anim", out (under extracted), ue_dir, ue_skeleton
  clips          exact controller names, or
  clip_patterns  regexes over controller names, with optional "limit", "offset" and "min_frames"
  channel_maps   body channel maps to accept (default the two player body maps)
  preview        {"dressed": "/Game/MaddenRef/Players/Stage2", "spacing": 300, "per_row": 10} or omitted
"""
import csv
import json
import os
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(REPO, "tools", "madden"))
csv.field_size_limit(sys.maxsize)
INV = os.path.join(REPO, "extracted", "animation", "animation_inventory.csv")
JOBS = os.path.join(REPO, "tools", "madden", "jobs")
LOGS = os.path.join(REPO, "logs")
BODY_MAPS = ("6a6eb813061beab6", "e5b9c7b4b01de68d")
BYTES_PER_FRAME = 50 * 1024   # observed about 43 KB of ASCII FBX per frame (268 bones, 6 curves)
STAGE2_PARTS = ["SK_faulkmarshall_11413", "SK_fullarms_d", "SK_legs_d", "SK_jersey_d", "SK_pants_d", "SK_socks_mid_d", "SK_sleeve_tight_d",
                "SK_helmet_shell_xenith", "SK_helmet_facemask_xenithprism", "SK_shoe_low_nikealphamenaceelite99clubr_d",
                "SK_shoe_low_nikealphamenaceelite99clubl_d", "SK_glover_d", "SK_glovel_d"]


def load_spec(p):
    s = json.load(open(p, encoding="utf-8"))
    s["OUTDIR"] = os.path.join(REPO, "extracted", *s["out"].split("/"))
    return s


def safe(name):
    return re.sub(r"[^A-Za-z0-9_]+", "_", name).strip("_")


def cmd_plan(spec):
    rows = list(csv.DictReader(open(INV, encoding="utf-8")))
    maps = tuple(spec.get("channel_maps", BODY_MAPS))
    body = {}
    layer = {}
    for r in rows:
        if r["role"] == "Anim" and r["channel_to_dof"] in maps:
            body.setdefault(r["name"], r)
        elif r["role"] == "LayerAnim":
            layer.setdefault(r["name"], r)
    names, missing = [], []
    if spec.get("clips"):
        for n in spec["clips"]:
            (names if n in body else missing).append(n)
    for pat in spec.get("clip_patterns", []):
        rx = re.compile(pat, re.I)
        for n in sorted(body):
            if rx.search(n) and int(body[n]["frames"]) >= spec.get("min_frames", 2) and n not in names:
                names.append(n)
    if spec.get("offset") or spec.get("limit"):
        a = spec.get("offset", 0)
        names = names[a:a + spec["limit"]] if spec.get("limit") else names[a:]
    jobs, ue, frames = [], [], 0
    for n in names:
        b = body[n]
        l = layer.get(n)
        fname = safe(n)
        jobs.append({"name": n, "file": fname, "clip_key": b["clip_key"], "layer_key": l["clip_key"] if l else None,
                     "fps": float(b["fps"]), "frames": int(b["frames"])})
        ue.append({"kind": "anim", "file": "{REPO}/extracted/" + spec["out"] + "/" + fname + ".fbx", "dest": spec["ue_dir"],
                   "name": "A_" + fname, "skeleton": spec["ue_skeleton"], "fps": float(b["fps"])})
        frames += int(b["frames"])
    json.dump(jobs, open(os.path.join(JOBS, spec["name"] + "_anim.json"), "w"), indent=1)
    json.dump(ue, open(os.path.join(JOBS, "ue_" + spec["name"] + ".json"), "w"), indent=1)
    summary = {"kit": spec["name"], "clips": len(jobs), "frames": frames, "missing": missing,
               "projected_bytes": frames * BYTES_PER_FRAME, "ue_jobs": len(ue)}
    json.dump(summary, open(os.path.join(LOGS, "kit_%s_summary.json" % spec["name"]), "w"), indent=1)
    print(json.dumps({k: v for k, v in summary.items() if k != "missing"}), "missing", missing)
    return 0


def plausible(r):
    """Gate per clip. Thresholds are loose on purpose, they catch broken decodes, not unusual motion."""
    problems = []
    if not r["finite"]:
        problems.append("non finite values")
    if r["bone_length_rel_dev_max"] > 1e-4:
        problems.append("bone length varies %.2e (%s)" % (r["bone_length_rel_dev_max"], r["bone_length_worst"]))
    lo, hi = r["head_height_m"]
    if hi > 2.6 or hi < 0.2:
        problems.append("head height %.2f to %.2f m" % (lo, hi))
    if r["lowest_foot_m"] < -0.3:
        problems.append("foot %.2f m below ground" % r["lowest_foot_m"])
    if r["traj_speed_mps"] > 15:
        problems.append("root speed %.1f m/s" % r["traj_speed_mps"])
    return problems


def cmd_export(spec):
    import madden
    import checkb
    import fbxanim
    t0 = time.time()
    jobs = json.load(open(os.path.join(JOBS, spec["name"] + "_anim.json")))
    os.makedirs(spec["OUTDIR"], exist_ok=True)
    checks_path = os.path.join(LOGS, "kit_%s_checks.json" % spec["name"])
    checks = json.load(open(checks_path)) if os.path.exists(checks_path) else {}
    ex = fbxanim.Exporter()
    ex.ev._fps = {k: float(o.get("FPS") or 30.0) for k, o in ex.ctrl.items()}
    m = madden.load_manifest()
    done = skipped = failed = 0
    for j in jobs:
        fbx = os.path.join(spec["OUTDIR"], j["file"] + ".fbx")
        meta = os.path.join(spec["OUTDIR"], j["file"] + ".json")
        rec = m["files"].get(madden.rel(fbx))
        if j["name"] in checks and checks[j["name"]]["ok"] and os.path.exists(fbx) and rec and rec.get("sha256") == madden.sha256(fbx):
            skipped += 1
            continue
        try:
            r, _ = checkb.evaluate(ex.ev, int(j["clip_key"], 16))
            problems = plausible(r)
            checks[j["name"]] = {"ok": not problems, "problems": problems,
                                 **{k: r[k] for k in ("frames", "duration_s", "traj_speed_mps", "head_height_m", "lowest_foot_m",
                                                      "bone_length_rel_dev_max", "max_deg_per_frame")}}
            if problems:
                failed += 1
                madden.append_failure(spec["name"], j["name"], "anim_plausibility", "; ".join(problems))
                continue
            ex.export(spec["OUTDIR"], int(j["clip_key"], 16), int(j["layer_key"], 16) if j["layer_key"] else None, skip_existing=False,
                      sparse=bool(spec.get("sparse", False)))
            for p, op in ((fbx, "fbxanim"), (meta, "fbxanim_meta")):
                m["files"][madden.rel(p)] = {"op": op, "stage": spec["name"], "source_asset": "%s (clip %s, layer %s)" % (j["name"], j["clip_key"], j["layer_key"]),
                                             "bundles": "levels/maddengame + launch_sba AssetBanks", "superbundles": "",
                                             "size": os.path.getsize(p), "sha256": madden.sha256(p), "params": {"fps": j["fps"]}}
            done += 1
        except Exception as e:
            failed += 1
            checks[j["name"]] = {"ok": False, "problems": ["decode error " + repr(e)[:200]]}
            madden.append_failure(spec["name"], j["name"], "anim_decode", repr(e)[:300])
        if (done + failed) % 25 == 0:
            madden.save_manifest(m)
            json.dump(checks, open(checks_path, "w"), indent=1)
    madden.save_manifest(m)
    json.dump(checks, open(checks_path, "w"), indent=1)
    # leave failed clips out of the Unreal jobs
    ue_path = os.path.join(JOBS, "ue_" + spec["name"] + ".json")
    ue = json.load(open(ue_path))
    bad = {"A_" + safe(n) for n, c in checks.items() if not c["ok"]}
    json.dump([u for u in ue if u["name"] not in bad], open(os.path.join(LOGS, "ue_%s_gated.json" % spec["name"]), "w"), indent=1)
    print("anim export %s done=%d skipped=%d failed=%d in %.0fs" % (spec["name"], done, skipped, failed, time.time() - t0))
    return 0


def cmd_mat(spec):
    pv = spec.get("preview")
    if not pv:
        json.dump({"bindings": []}, open(os.path.join(JOBS, "mat_" + spec["name"] + ".json"), "w"))
        return 0
    gated = json.load(open(os.path.join(LOGS, "ue_%s_gated.json" % spec["name"])))
    sp, per = pv.get("spacing", 300.0), pv.get("per_row", 10)
    meshes = []
    for i, u in enumerate(gated):
        x, y = (i % per) * sp, (i // per) * sp
        for p in STAGE2_PARTS:
            meshes.append({"mesh": pv["dressed"] + "/" + p, "x": x, "y": y, "label": u["name"][2:30], "anim": u["dest"] + "/" + u["name"]})
    level = "/Game/MaddenRef/Maps/L_MaddenRef_" + pv.get("level", safe(spec["name"].title()))
    json.dump({"bindings": [], "preview_level": {"path": level, "meshes": meshes}}, open(os.path.join(JOBS, "mat_" + spec["name"] + ".json"), "w"), indent=1)
    print("preview", level, len(gated), "clips")
    return 0


if __name__ == "__main__":
    sys.exit({"plan": cmd_plan, "export": cmd_export, "mat": cmd_mat}[sys.argv[1]](load_spec(sys.argv[2])))
