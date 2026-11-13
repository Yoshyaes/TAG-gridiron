r"""Plans the TAG character meshes built from the personal model pack (D:\MaddenRef\ModelPack) and runs the builds.

  py -3.13 -I Tools/Art/hero_jobs.py plan                 RawArt/Hero/jobs.json (+ head skin tones, cached)
  py -3.13 -I Tools/Art/hero_jobs.py build [workers] [--only=<regex>] [--force]   Blender, hero_build.py
  py -3.13 -I Tools/Art/hero_jobs.py index                RawArt/Hero/hero_index.json (what the importer reads)

Groups (asset name prefix):
  Kit_<build>_<style>     player body: arms, legs, jersey, sleeves, pants, socks, cleats, gloves / tape / pads.
                          build t thin, d default, m muscular, h heavy; style skill, line, qb, kick
  Helmet_<model>_<mask>   shell + facemask + chin strap / padding (skinned to the head)
  Head_<id>               player head + eyes + lashes + hair + hair cap (hair is its own slot so a helmet can hide it)
  CoachBody_<b>_<top>     coach body (polo, windbreaker, rain jacket; builds b, m, h)
  CoachHead_<id>          generic coach heads with cap and headset (the named, real coaches are left out)
  Ref_<warm|cold>         referee body; RefHead_<id> referee head + cap
  Crew_<name>, Fan_<name> sideline crew and spectators on the 34 bone crowd rig (own skeleton)
Personal use only, like the pack. Output stays in the git-ignored RawArt/Hero.
"""
import csv
import glob
import json
import os
import re
import subprocess
import sys
import time

PACK = r"D:\MaddenRef\ModelPack"
HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.abspath(os.path.join(HERE, "..", ".."))
OUT = os.path.join(PROJ, "RawArt", "Hero")
BLENDER = r"C:\Program Files\Blender Foundation\Blender 5.2\blender.exe"
SKIN = "Textures/characters/common/textures/skintones/"
HEAD_COUNT = int(os.environ.get("TAG_HERO_HEADS", "240"))


def ap(rel):
    return os.path.join(PACK, *rel.replace("\\", "/").split("/"))


def sidecar(fbx):
    p = fbx[:-4] + ".materials.json"
    return json.load(open(p, encoding="utf-8")) if os.path.exists(p) else {"sections": []}


def textures(fbx):
    out = {}
    for s in sidecar(fbx)["sections"]:
        out[s["name"]] = {k: ap(v) for k, v in s["textures"].items() if isinstance(v, str)}
    return out


def part(rel, slot_rule):
    """rel: Models/... path without .fbx; slot_rule: a slot name for every section, or a function section -> slot."""
    fbx = ap(rel + ".fbx")
    if not os.path.exists(fbx):
        raise FileNotFoundError(fbx)
    tex = textures(fbx)
    slots = {}
    for sec in tex:
        slots[sec] = slot_rule(sec) if callable(slot_rule) else slot_rule
    if not tex:
        slots["*"] = slot_rule if not callable(slot_rule) else None
    return {"fbx": fbx, "slots": slots, "textures": tex}


def head_slot(sec):
    s = sec.lower()
    if s.startswith("head_lod0"):
        return "Head"
    if s.startswith("eyes"):
        return "Eyes"
    if "lash" in s:
        return "Lashes"
    if "haircap" in s:
        return "HairCap"
    if "hair" in s or s == "lambert3":
        return "Hair"
    if "headband" in s or "bandana" in s:
        return "HeadGear"
    return "Head"


def job(name, kind, parts, rig="hero", **meta):
    return {"name": name, "kind": kind, "rig": rig, "out": os.path.join(OUT, kind, name + ".fbx"), "parts": parts, "meta": meta}


G = "Models/Players/Gear/"
B = "Models/Players/Bodies/"
SHOES = {"skill": "Footwear/shoe_low_nikealphamenaceelite99club/shoe_low_nikealphamenaceelite99club%s_%s_mesh",
         "line": "Footwear/shoe_mid_nikealphamenacepro/shoe_mid_nikealphamenacepro%s_%s_mesh",
         "qb": "Footwear/shoe_low_nikevaporuntouchablepro/shoe_low_nikevaporuntouchablepro%s_%s_mesh",
         "kick": "Footwear/shoe_low_nikevaporspeed3/shoe_low_nikevaporspeed3%s_%s_mesh"}
KITS = {
    # style: sleeves, socks, hands (left, right), extra arm gear
    "skill": ("sleeve_tight", "socks_mid", ("ArmGear/glovel/glovel", "ArmGear/glover/glover"), ()),
    "line": ("sleeve_tight", "socks_high", ("ArmGear/glovetapednormall/glovetapednormall", "ArmGear/glovetapednormalr/glovetapednormalr"),
             ("ArmGear/elbowpadrubberl/elbowpadrubberl", "ArmGear/elbowpadrubberr/elbowpadrubberr")),
    "qb": ("sleeve_standard", "socks_mid", ("ArmGear/glovel/glovel", "ArmGear/wristbandnormalr/wristbandnormalr"), ()),
    "kick": ("sleeve_standard", "socks_low", ("ArmGear/wristbandnormall/wristbandnormall", "ArmGear/wristbandnormalr/wristbandnormalr"), ()),
}
HELMETS = [  # (shell model, facemask folder model, facemask, style tag)
    ("speedflex", "speedflex", "speedflex2bar", "open"), ("speedflex", "speedflex", "speedflex3barrb", "std"),
    ("speedflex", "speedflex", "speedflexcage", "cage"), ("revospeed", "revospeed", "revospeed2bar", "open"),
    ("revospeed", "revospeed", "revospeed3barlb", "std"), ("revospeed", "revospeed", "revospeedfullcage", "cage"),
    ("riddell360", "riddell360", "riddell3603barlb", "std"), ("riddell360", "riddell360", "riddell360robot", "cage"),
    ("vengeance", "vengeance", "vengeance3barrb", "std"), ("xenith", "xenith", "xenithprism", "open"), ("revo", "revo", "revokicker", "kick"),
    ("revo", "revo", "revofullcage", "cage"),
]


def plan_kits():
    jobs = []
    for b in "tdmh":
        for style, (sleeve, socks, hands, extra) in KITS.items():
            parts = [part(B + "fullarms_%s_mesh" % b, "Skin"), part(B + "legs_%s_mesh" % b, "SkinLegs"),
                     part(G + "TorsoApparel/jersey/jersey_%s_mesh" % b, "Jersey"),
                     part(G + "TorsoApparel/%s/%s_%s_mesh" % (sleeve, sleeve, b), "Jersey"),
                     part(G + "LegsApparel/pants/pants_%s_mesh" % b, "Pants"),
                     part(G + "LegsApparel/%s/%s_%s_mesh" % (socks, socks, b), "Socks")]
            for lr in "lr":
                parts.append(part(G + SHOES[style] % (lr, b), "Shoes"))
            for h in hands:
                parts.append(part(G + h + "_%s_mesh" % b, "Hands"))
            for e in extra:
                parts.append(part(G + e + "_%s_mesh" % b, "ArmGear"))
            jobs.append(job("Kit_%s_%s" % (b, style), "Kit", parts, build=b, style=style))
    # physics proxy (weights collapsed to the main bones): the importer makes the shared ragdoll physics asset from it
    p = dict(jobs[[j["name"] for j in jobs].index("Kit_d_line")])
    jobs.append(dict(job("SKM_TAG_HeroRig", "Rig", p["parts"]), collapse=True))
    return jobs


def find_one(pattern):
    hits = glob.glob(ap(pattern))
    return os.path.relpath(hits[0], PACK)[:-4].replace("\\", "/") if hits else None


def plan_helmets():
    jobs = []
    for shell, folder, mask, style in HELMETS:
        parts = [part(find_one(G + "Helmets/%s/helmet_shell_*_mesh.fbx" % shell), "Shell")]
        acc = find_one(G + "Helmets/%s/helmet_accessories_*_mesh.fbx" % shell)
        if acc:
            parts.append(part(acc, "HelmetAcc"))
        if mask is None:
            m = find_one(G + "Helmets/%s/facemasks/helmet_facemask_*_mesh.fbx" % folder)
            mask = m.split("helmet_facemask_")[1][:-5] if m else None
        if mask:
            fm = find_one(G + "Helmets/*/facemasks/helmet_facemask_%s_mesh.fbx" % mask)
            if fm:
                parts.append(part(fm, "Facemask"))
        jobs.append(job("Helmet_%s_%s" % (shell, mask or "none"), "Helmet", parts, style=style, shell=shell))
    return jobs


def skin_tone(png, cache):
    """Nearest of the pack's 8 arm skin tones to the face (centre third of the head texture)."""
    if png in cache:
        return cache[png]
    from PIL import Image
    tones = cache.setdefault("__tones__", {})
    if not tones:
        for n in range(1, 9):
            im = Image.open(ap(SKIN + "arm_skintone%d.png" % n)).convert("RGB").resize((32, 32))
            px = list(im.getdata())
            tones[n] = [sum(c[i] for c in px) / len(px) for i in range(3)]
    try:
        im = Image.open(png).convert("RGB")
        w, h = im.size
        face = im.crop((w // 3, h // 3, 2 * w // 3, 2 * h // 3)).resize((32, 32))
        px = list(face.getdata())
        t = [sum(c[i] for c in px) / len(px) for i in range(3)]
        best = min(tones, key=lambda n: sum((a - b) ** 2 for a, b in zip(tones[n], t)))
    except Exception:
        best = 1
    cache[png] = int(best)
    return int(best)


def plan_heads():
    cache_p = os.path.join(OUT, "head_tones.json")
    cache = json.load(open(cache_p)) if os.path.exists(cache_p) else {}
    cache.pop("__tones__", None)
    heads = []
    for d in sorted(glob.glob(ap("Models/Players/Heads/*/"))):
        who = os.path.basename(d.rstrip("\\/"))
        main = os.path.join(d, who + "_mesh.fbx")
        if not os.path.exists(main):
            continue
        tex = textures(main)
        col = next((t.get("colorTexture") for s, t in tex.items() if s.startswith("head_lod0")), None)
        if not col or not os.path.exists(col):
            continue
        heads.append((who, d, skin_tone(col, cache)))
    cache.pop("__tones__", None)
    os.makedirs(OUT, exist_ok=True)
    json.dump(cache, open(cache_p, "w"))
    # an even spread over the tones the roster has, in a stable order (sorted names, every k-th head)
    by_tone = {}
    for h in heads:
        by_tone.setdefault(h[2], []).append(h)
    quota = {t: max(4, round(HEAD_COUNT * len(v) / len(heads))) for t, v in by_tone.items()}
    jobs = []
    for t, v in sorted(by_tone.items()):
        step = max(1, len(v) // quota[t])
        for who, d, tone in v[::step][:quota[t]]:
            parts = []
            for f in sorted(glob.glob(os.path.join(d, "*_mesh.fbx"))):
                if "helmet_on" in f:
                    continue
                rel = os.path.relpath(f, PACK)[:-4].replace("\\", "/")
                parts.append(part(rel, head_slot))
            jobs.append(job("Head_" + re.sub(r"[^A-Za-z0-9_]", "_", who), "Head", parts, tone=tone, source=who))
    return jobs


def plan_staff():
    jobs = []
    C = "Models/Coaches/"
    for b in "bmh":
        base = [part(C + "Bodies/coachreferee_%s/coachreferee_%s_%s_mesh" % (b, b, p), slot)
                for p, slot in (("arm", "Skin"), ("pants", "Pants"), ("shoedress", "Shoes"), ("watch", "ArmGear"))]
        for top in ("poloshirt", "windbreaker", "rainjacket"):
            jobs.append(job("CoachBody_%s_%s" % (b, top), "Coach", base + [part(C + "Bodies/coachreferee_%s/coachreferee_%s_%s_mesh" % (b, b, top), "Top")],
                            build=b, top=top))
    # generic coach heads only: folders named like 2_b_m_01 / c_1_b_n_01 (the rest are real, named coaches)
    for d in sorted(glob.glob(ap(C + "Named/*/"))):
        who = os.path.basename(d.rstrip("\\/"))
        if not re.match(r"^(c_)?\d_[a-z]_[a-z]{1,2}_\d\d$", who):
            continue
        parts = []
        for f in sorted(glob.glob(os.path.join(d, "*_mesh.fbx"))):
            rel = os.path.relpath(f, PACK)[:-4].replace("\\", "/")
            n = os.path.basename(f).lower()
            rule = "Hat" if "_hat" in n or "visor" in n else "Headset" if "headset" in n else "Glasses" if "glasses" in n else head_slot
            parts.append(part(rel, rule))
        tex = textures(ap(os.path.relpath(glob.glob(os.path.join(d, "*head_mesh.fbx"))[0], PACK)) if glob.glob(os.path.join(d, "*head_mesh.fbx")) else "")
        col = next((t.get("colorTexture") for s, t in tex.items() if s.startswith("head_lod0")), None) if tex else None
        jobs.append(job("CoachHead_" + who, "CoachHead", parts, source=who, tone=skin_tone(col, {}) if col else 1,
                        hat=any("_hat" in os.path.basename(p["fbx"]).lower() for p in parts)))
    R = "Models/Referees/"
    common = [part(R + "common/referee_lowerbody_mesh", "Pants"), part(R + "common/referee_accessories_mesh", "ArmGear")]
    jobs.append(job("Ref_warm", "Ref", common + [part(R + "warm/referee_upperbody_warm_mesh", "Top"), part(R + "warm/referee_arms_warm_mesh", "Skin")]))
    jobs.append(job("Ref_cold", "Ref", common + [part(R + "cold/referee_upperbody_cold_mesh", "Top")]))
    rh = part(R + "common/ref_1_b_n_01_mesh", head_slot)
    col = next((t.get("colorTexture") for s, t in rh["textures"].items() if s.startswith("head_lod0")), None)
    jobs.append(job("RefHead_1", "RefHead", [rh, part(R + "common/referee_hat_mesh", "Hat")], tone=skin_tone(col, {}) if col else 1))
    return jobs


def plan_crowd():
    jobs = []
    for f in sorted(glob.glob(ap("Models/Sidelines/*_swarm_mesh.fbx")) + glob.glob(ap("Models/Crowd/male/*_swarm_mesh.fbx")) +
                    glob.glob(ap("Models/Crowd/female/*_swarm_mesh.fbx"))):
        rel = os.path.relpath(f, PACK)[:-4].replace("\\", "/")
        n = os.path.basename(f).replace("_swarm_mesh.fbx", "")
        if rel.startswith("Models/Sidelines") and not re.search(r"chaingang|teamtrainer|asstcoach|handheldslr|steadicam|broadcastvideo", n):
            continue
        kind = "Crew" if rel.startswith("Models/Sidelines") else "Fan"
        p = part(rel, "Body")
        col = next((t.get("swarmColor") or t.get("colorTexture") for t in p["textures"].values()), None)
        jobs.append(job("%s_%s" % (kind, n), kind, [p], rig="crowd", source=n, cells=atlas_grid(col) if "swarmColor" in json.dumps(p["textures"]) else 1))
    # the crowd skeleton's bootstrap mesh (first fan)
    fan = next(j for j in jobs if j["kind"] == "Fan")
    jobs.append(job("SKM_TAG_CrowdRig", "Rig", fan["parts"], rig="crowd"))
    return jobs


def atlas_grid(path):
    """Crowd atlases hold N x N variants of one person (N is 2 or 4), found from the picture's own periodicity."""
    if not path or not os.path.exists(path):
        return 1
    from PIL import Image
    im = Image.open(path).convert("L").resize((64, 64))
    g = [v / 255.0 for v in im.getdata()]

    def shift_diff(step):
        return sum(abs(g[y * 64 + x] - g[y * 64 + (x + step) % 64]) for y in range(64) for x in range(64)) / 4096
    d8, d4, d2 = shift_diff(8), shift_diff(16), shift_diff(32)
    if d4 < 0.5 * d8:
        return 4
    if d2 < 0.5 * d4:
        return 2
    return 1


def rig_lookup():
    import csv
    rig = {}
    for r in csv.DictReader(open(os.path.join(PACK, "manifest.csv"), encoding="utf-8")):
        rig[os.path.normcase(os.path.join(PACK, *r["fbx"].split("/")))] = r["skeleton"]
    return lambda f: rig.get(os.path.normcase(f), "hero")


def cmd_plan():
    """The whole pack (hero_full.py): outfits, every helmet, every assembled character; officials and rig bootstraps."""
    sys.path.insert(0, HERE)
    import hero_full as hf
    jobs = hf.plan_outfits(job)
    proxy = next(j for j in jobs if j["meta"]["style"] == "line" and j["meta"]["build"] == "d")
    jobs.append(dict(job("SKM_TAG_HeroRig", "Rig", proxy["parts"]), collapse=True))
    jobs += hf.plan_helmets(job)
    jobs += hf.plan_recipes(job, rig_lookup())
    jobs += [j for j in plan_staff() if j["kind"] in ("Ref", "RefHead")]
    # skin tone of every face (arms and legs are painted to match), atlas cells of the fans
    cache_p = os.path.join(OUT, "head_tones.json")
    cache = json.load(open(cache_p)) if os.path.exists(cache_p) else {}
    for j in jobs:
        if j["kind"] in ("Head", "CoachHead", "CoachFull", "Story", "Staff"):
            col = next((t.get("colorTexture") for p in j["parts"] for sec, t in p["textures"].items() if p["slots"].get(sec) == "Head"), None)
            if col:
                j["meta"]["tone"] = skin_tone(col, cache)
        if j["kind"] in ("Fan", "FanProp"):
            col = next((t.get("swarmColor") or t.get("colorTexture") for p in j["parts"] for t in p["textures"].values()), None)
            j["meta"]["cells"] = atlas_grid(col) if col and "swarm" in col.lower() else 1
    cache.pop("__tones__", None)
    json.dump(cache, open(cache_p, "w"))
    # one bootstrap mesh per skeleton family (its skeleton is created from it)
    for rig, boot in (("crowd", "SKM_TAG_CrowdRig"), ("male", "SKM_TAG_MaleRig"), ("female", "SKM_TAG_FemaleRig"),
                      ("child", "SKM_TAG_ChildRig"), ("sideplayer", "SKM_TAG_SidePlayerRig")):
        first = next((j for j in jobs if j["rig"] == rig and j["kind"] != "Rig"), None)
        if first:
            jobs.append(job(boot, "Rig", first["parts"], rig=rig))
    os.makedirs(OUT, exist_ok=True)
    json.dump(jobs, open(os.path.join(OUT, "jobs.json"), "w"), indent=1)
    kinds = {}
    for j in jobs:
        kinds[j["kind"]] = kinds.get(j["kind"], 0) + 1
    print("jobs", len(jobs), kinds)


def cmd_build():
    args = sys.argv[2:]
    workers = int(args[0]) if args and args[0].isdigit() else 6
    jobs = json.load(open(os.path.join(OUT, "jobs.json")))
    only = [a[7:] for a in args if a.startswith("--only=")]
    if only:
        rx = re.compile(only[0], re.I)
        jobs = [j for j in jobs if rx.search(j["name"])]
    if "--force" in args:
        for j in jobs:
            j["force"] = True
    todo = [j for j in jobs if j.get("force") or not os.path.exists(j["out"][:-4] + ".json")]
    print("build jobs", len(jobs), "todo", len(todo), flush=True)
    res = os.path.join(OUT, "build_results.jsonl")
    procs = []
    for w in range(workers):
        mine = todo[w::workers]
        if not mine:
            continue
        jp = os.path.join(OUT, "_work_%02d.json" % w)
        json.dump(mine, open(jp, "w"))
        log = open(os.path.join(OUT, "_work_%02d.log" % w), "w")
        procs.append(subprocess.Popen([BLENDER, "-b", "--factory-startup", "--python", os.path.join(HERE, "hero_build.py"), "--", jp, res],
                                      stdout=log, stderr=subprocess.STDOUT))
    t0 = time.time()
    while any(p.poll() is None for p in procs):
        time.sleep(15)
        done = sum(1 for j in todo if os.path.exists(j["out"][:-4] + ".json"))
        print("%5.0fs built %d / %d" % (time.time() - t0, done, len(todo)), flush=True)
    fails = [j["name"] for j in todo if not os.path.exists(j["out"][:-4] + ".json")]
    print("workers exited", [p.returncode for p in procs], "failed", len(fails), fails[:20])


def cmd_index():
    jobs = json.load(open(os.path.join(OUT, "jobs.json")))
    out = []
    for j in jobs:
        side = j["out"][:-4] + ".json"
        if os.path.exists(side):
            d = json.load(open(side))
            d["fbx"] = j["out"]
            if j.get("uv_sets"):
                d["uv_sets"] = j["uv_sets"]  # the importer's materials read it (helmet shells: detail normal on TexCoord 1)
            out.append(d)
    json.dump(out, open(os.path.join(OUT, "hero_index.json"), "w"), indent=1)
    print("indexed", len(out), "of", len(jobs))


if __name__ == "__main__":
    {"plan": cmd_plan, "build": cmd_build, "index": cmd_index}[sys.argv[1]]()
