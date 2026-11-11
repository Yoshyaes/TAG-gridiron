r"""Whole-pack coverage for the model pack characters (imported by hero_jobs.py plan).

Every assembled character the pack defines (its own recipes, _work/assemble_jobs.json): all 1,254 player heads, the 328
coach characters (named coaches with their own warm / cold bodies, and the heads), the 105 coach bodies (coach-referee
and create-a-coach), the 47 sideline people (crew, media, coordinators, TV cameramen, sideline players), the 64 fans,
the 3 crowd props, the 166 story characters. Plus player outfits generated so that every piece of player gear the pack
holds (cleats, spats, gloves, tape, wrist and elbow gear, sleeves and jerseys for every pad shape, every pants pad cut,
socks, towels, neck rolls, hand warmers, flak jackets, back plates) is worn by at least one outfit of every body type,
every helmet shell with every facemask, and the sideline ball caps.

Slots: a part's sections get a role (Skin, Head, Eyes, Hair, HairCap, Lashes, Top, Bottom, Shoes, Hat, Headset,
Glasses, Gear) and a numeric suffix when one character has two textures for the same role. Each section's colour
texture is measured: mean luminance (the recolour material's reference) and whether it is a team-colour placeholder
(flat neon green; the pure blue / red the coach clothes use), so the game can paint them in team colours.
"""
import glob
import json
import os
import re

PACK = r"D:\MaddenRef\ModelPack"
G = "Models/Players/Gear/"
B = "Models/Players/Bodies/"
_LUM = {}


def ap(rel):
    return os.path.join(PACK, *rel.replace("\\", "/").split("/"))


_WHITE = {}


def white_fraction(png):
    """Share of near-white pixels: the pack's blank face texture (no face exported) is about half white."""
    if png not in _WHITE:
        try:
            from PIL import Image
            px = list(Image.open(png).convert("RGB").resize((64, 64)).getdata())
            _WHITE[png] = sum(1 for p in px if min(p) > 230) / len(px)
        except Exception:
            _WHITE[png] = 0.0
    return _WHITE[png]


def rainbow(png):
    """The game's own missing-texture placeholder: full-saturation hue bands with the asset name written over them."""
    try:
        import colorsys
        from PIL import Image
        px = list(Image.open(png).convert("RGB").resize((32, 8)).getdata())
        bins = set()
        for r, g, b in px:
            h, s, v = colorsys.rgb_to_hsv(r / 255.0, g / 255.0, b / 255.0)
            if s > 0.6 and v > 0.5:
                bins.add(int(h * 12))
        return len(bins) >= 7
    except Exception:
        return False


def measure(png):
    """(mean luminance 0..1, placeholder flag) of a colour texture."""
    if not png:
        return 1.0, False
    if png in _LUM:
        return _LUM[png]
    try:
        from PIL import Image
        im = Image.open(png).convert("RGB").resize((16, 16))
        px = list(im.getdata())
        r = sum(p[0] for p in px) / len(px) / 255.0
        g = sum(p[1] for p in px) / len(px) / 255.0
        b = sum(p[2] for p in px) / len(px) / 255.0
        lum = 0.3 * r + 0.59 * g + 0.11 * b
        neon = g > 0.7 and r < 0.75 and b < 0.3 and g > r + 0.25   # shaded neon green too (same test as the material)
        blue = b > 0.75 and r < 0.3 and g < 0.2          # coach trousers placeholder
        red = r > 0.35 and g < 0.08 and b < 0.3          # coach tops placeholder (team primary)
        import colorsys
        sat = colorsys.rgb_to_hsv(r, g, b)[1]
        res = (round(max(lum, 0.02), 4), bool(neon or blue or red or (sat > 0.65 and max(r, g, b) > 0.3)))
        if rainbow(png):
            res = (1.0, "rainbow")
    except Exception:
        res = (1.0, False)
    _LUM[png] = res
    return res


RULES = [("Shoes", r"shoe|sneaker|boot|heel|cleat|sandal"), ("Gear", r"watch|bag|glove|wristband|belt|jewel|necklace|bracelet|earring|accessor|lanyard|whistle"),
         ("Headset", r"headset"), ("Glasses", r"glasses|sunglass"), ("Hat", r"(^|_)hat|ballcap|visorhat|beanie|(^|_)cap(_|$)|toque"),
         ("Skin", r"skin|(^|_)arms?(_|$)|fullarms"), ("Bottom", r"lower|pant|bottom|short|jean|khaki|skirt|trouser"),
         ("Top", r"upper|(^|_)top|shirt|jacket|polo|windbreaker|sweater|hoodie|vest|coat|blouse")]


def role_of(part_name, sec):
    """Material role of a section: eyes / lashes / hair by the section, then by the part's own name, then the section."""
    pn, sn = part_name.lower(), sec.lower()
    if re.search(r"lash", sn):
        return "Lashes"
    if sn.startswith("eye") or re.search(r"(^|_)eyes?_", sn):
        return "Eyes"
    if "haircap" in sn or "haircap" in pn:
        return "HairCap"
    if re.search(r"hair|beard|stubble|goatee|mustache|lambert3", sn) or re.search(r"hair|beard", pn):
        return "Hair"
    if sn.startswith("head") and not re.search(r"band|set", sn):
        return "Head"
    for text in (pn, sn):
        for role, rx in RULES:
            if re.search(rx, text):
                return role
        if re.search(r"(^|_)head(_|$)", text):
            return "Head"
    return "Gear"


def sections_of(fbx):
    p = fbx[:-4] + ".materials.json"
    return json.load(open(p, encoding="utf-8"))["sections"] if os.path.exists(p) else []


def build_part(fbx, rule=None, force_role=None):
    """Part entry: slots by role (rule(section) when given), textures per section with the measurements."""
    secs = sections_of(fbx)
    name = os.path.basename(fbx)[:-9] if fbx.endswith("_mesh.fbx") else os.path.basename(fbx)[:-4]
    slots, tex = {}, {}
    for s in secs:
        t = {k: ap(v) for k, v in s["textures"].items() if isinstance(v, str)}
        col = t.get("colorTexture") or t.get("swarmColor")
        lum, ph = measure(col)
        t["_lum"] = lum
        t["_placeholder"] = ph
        tex[s["name"]] = t
        slots[s["name"]] = force_role or (rule(s["name"]) if rule else role_of(name, s["name"]))
    if not secs:
        slots["*"] = force_role or role_of(name, "")
    return {"fbx": fbx, "slots": slots, "textures": tex}


def unique_slots(parts):
    """Two different textures under one role in one character get Role, Role_2, ... (one material slot each)."""
    fixed = {"Head", "Eyes", "Lashes", "Hair", "HairCap", "Skin", "SkinLegs", "Jersey", "Pants", "Socks", "Shell", "Facemask", "Body"}
    owner = {}
    for p in parts:
        for sec, role in list(p["slots"].items()):
            if role is None or role in fixed:
                continue
            t = p["textures"].get(sec, {})
            key = t.get("colorTexture") or t.get("swarmColor") or (p["fbx"] + ":" + sec)
            n = 1
            while True:
                slot = role if n == 1 else "%s_%d" % (role, n)
                if owner.setdefault(slot, key) == key:
                    break
                n += 1
            p["slots"][sec] = slot
    return parts


# ------------------------------------------------------------------ player outfits
def gear_dirs(sub, exclude=r"oldfallback"):
    return sorted(d for d in glob.glob(ap(G + sub + "/*/")) if not re.search(exclude, d))


def files_for(d, b):
    return sorted(f for f in glob.glob(os.path.join(d, "*_%s_mesh.fbx" % b)))


def plan_outfits(job):
    shoes = [d for d in gear_dirs("Footwear") if not re.search(r"[\\/]spat(bulky|thin)[\\/]$", d)]
    spats = [d for d in gear_dirs("Footwear") if re.search(r"[\\/]spat(bulky|thin)[\\/]$", d)]
    arm = gear_dirs("ArmGear")
    base = lambda d: os.path.basename(d.rstrip("\\/"))
    pairs = sorted({base(d)[:-1] for d in arm if base(d)[-1] in "lr" and base(d)[:-1] + ("r" if base(d)[-1] == "l" else "l") in {base(x) for x in arm}})
    hands = [p for p in pairs if re.match(r"glove|tapedhand|vintagebeast", p)]
    wrist = [p for p in pairs if re.match(r"wrist", p)]
    elbow = [p for p in pairs if re.match(r"elbow", p)]
    sleeves = ["sleeve_tight", "sleeve_standard", "sleeve_long"]
    pads = ["", "shoulderpadheight", "shoulderpadwidth"]
    pants = ["pants", "pants/kneepadlarge", "pants/kneepadsmall", "pants/thighpadlarge", "pants/thighpadsmall"]
    socks = ["socks_mid", "socks_high", "socks_low", "socks_under"]
    # back plate / flak jacket are jersey cuts (they replace the jersey); towel, neck roll, hand warmer go on top
    extras = ["", "TorsoGear/towel", "TorsoGear/vintageneckroll", "LegsApparel/handwarmer"]
    styles = ["skill", "line", "qb", "kick"]
    jobs = []
    n = max(len(shoes), len(hands) * 4, len(wrist) * 4, len(elbow) * 4, 45)
    for b in "tdmh":
        for i in range(n):
            style = styles[i % 4]
            pad = pads[(i // 4) % 3]
            sleeve = sleeves[(i // 12) % 3] if style != "line" else sleeves[(i // 12) % 2]
            parts = [build_part(ap(B + "fullarms_%s_mesh.fbx" % b), force_role="Skin"),
                     build_part(ap(B + "legs_%s_mesh.fbx" % b), force_role="SkinLegs")]
            cut = "flakjacket" if (style == "qb" and i % 8 < 4) else "backplate" if (style == "skill" and i % 12 == 4) else ""
            jersey = ("TorsoApparel/jersey/%s/jersey_%s_%s_mesh.fbx" % (cut, cut, b) if cut else
                      "TorsoApparel/jersey/jersey_%s_mesh.fbx" % b if not pad else "TorsoApparel/jersey/%s/jersey_%s_%s_mesh.fbx" % (pad, pad, b))
            sl = "TorsoApparel/%s/%s_%s_mesh.fbx" % (sleeve, sleeve, b) if not pad else "TorsoApparel/%s/%s/%s_%s_%s_mesh.fbx" % (sleeve, pad, sleeve, pad, b)
            for rel in (jersey, sl):
                if os.path.exists(ap(G + rel)):
                    parts.append(build_part(ap(G + rel), force_role="Jersey"))
            pc = pants[i % len(pants)]
            pf = ap(G + "LegsApparel/%s/%s_%s_mesh.fbx" % (pc, pc.replace("/", "_"), b))
            parts.append(build_part(pf if os.path.exists(pf) else ap(G + "LegsApparel/pants/pants_%s_mesh.fbx" % b), force_role="Pants"))
            sk = socks[(i // 2) % len(socks)]
            parts.append(build_part(ap(G + "LegsApparel/%s/%s_%s_mesh.fbx" % (sk, sk, b)), force_role="Socks"))
            for f in files_for(shoes[i % len(shoes)], b):
                parts.append(build_part(f, force_role="Shoes"))
            if i % 5 == 3 and spats:
                for f in files_for(spats[(i // 5) % len(spats)], b):
                    parts.append(build_part(f, force_role="Gear"))
            # hands: skill / line / qb (left glove, throwing hand bare or taped) / kick (no gloves)
            h = hands[i % len(hands)]
            if style == "qb":
                parts += [build_part(f, force_role="Hands") for f in files_for(ap(G + "ArmGear/%sl" % h), b)]
            elif style != "kick":
                parts += [build_part(f, force_role="Hands") for f in files_for(ap(G + "ArmGear/%sl" % h), b) + files_for(ap(G + "ArmGear/%sr" % h), b)]
            w = wrist[i % len(wrist)]
            if style in ("kick", "qb") or i % 3 == 0:
                parts += [build_part(f, force_role="Gear") for f in files_for(ap(G + "ArmGear/%sl" % w), b) + files_for(ap(G + "ArmGear/%sr" % w), b)]
            if style == "line" or i % 4 == 1 or i % 7 == 0:
                e = elbow[i % len(elbow)]
                parts += [build_part(f, force_role="Gear") for f in files_for(ap(G + "ArmGear/%sl" % e), b) + files_for(ap(G + "ArmGear/%sr" % e), b)]
            ex = extras[i % len(extras)]
            if ex:
                f = ap(G + "%s/%s_%s_mesh.fbx" % (ex, os.path.basename(ex), b))
                if os.path.exists(f):
                    parts.append(build_part(f, force_role="Gear"))
            jobs.append(job("Kit_%s_%s_%02d" % (b, style, i), "Kit", unique_slots(parts), build=b, style=style,
                            shoe=os.path.basename(shoes[i % len(shoes)].rstrip("\\/"))))
    return jobs


def mask_style(name):
    n = name.lower()
    if re.search(r"cage|robot|bull|grid|egjop|rjop|njop", n):
        return "cage"
    if re.search(r"kicker|single|onebar|towbar", n):
        return "kick"
    if re.search(r"2bar|qb|wr|808|egop|opo|vintage", n):
        return "open"
    return "std"


def plan_helmets(job):
    """Every shell with every facemask of its family (shells without their own facemasks take four shared ones each),
    a visor on every fourth, and the sideline caps."""
    jobs = []
    folders = sorted(d for d in glob.glob(ap(G + "Helmets/*/")) if glob.glob(os.path.join(d, "helmet_shell_*_mesh.fbx")))
    shared = sorted(glob.glob(ap(G + "Helmets/standard/facemasks/*_mesh.fbx")))
    k = 0
    for d in folders:
        own = sorted(glob.glob(os.path.join(d, "facemasks", "*_mesh.fbx")))
        for shell in sorted(glob.glob(os.path.join(d, "helmet_shell_*_mesh.fbx"))):
            sname = os.path.basename(shell)[len("helmet_shell_"):-9]
            acc = glob.glob(os.path.join(d, "helmet_accessories_%s_mesh.fbx" % sname)) or glob.glob(os.path.join(d, "helmet_accessories_*_mesh.fbx"))
            visor = glob.glob(os.path.join(d, "helmet_visor_%s_mesh.fbx" % sname)) or glob.glob(os.path.join(d, "helmet_visor_*_mesh.fbx"))
            masks = own or [shared[(k * 4 + i) % len(shared)] for i in range(4)]
            k += 0 if own else 1
            for mi, m in enumerate(masks):
                mname = os.path.basename(m)[len("helmet_facemask_"):-9]
                vis = bool(visor) and mi % 4 == 2
                parts = [dict(build_part(shell, force_role="Shell"), uv=1)]  # the shell's colour layout is UV set 1
                if acc:
                    parts.append(build_part(acc[0], force_role="HelmetAcc"))
                parts.append(build_part(m, force_role="Facemask"))
                if vis:
                    parts.append(build_part(visor[0], force_role="Visor"))
                jobs.append(job("Helmet_%s_%s%s" % (sname, mname, "_visor" if vis else ""), "Helmet", parts, style=mask_style(mname), shell=sname))
    for f in sorted(glob.glob(ap(G + "Helmets/hats/*_mesh.fbx"))):
        jobs.append(job("Cap_" + os.path.basename(f)[:-9], "Cap", [build_part(f, force_role="Hat")]))
    return jobs


# ------------------------------------------------------------------ the pack's own assembled characters
RIGS = {"hero": "hero", "crowd": "crowd", "male": "male", "female": "female", "child": "child", "sideline_player": "sideplayer"}


REPAIR = {"CruiseColt_Casual_Outfit_GP_F": "CruiseColt_Casual_Outfit_NGP_D", "LeGuinDylan_Casual_Outfit_GP_F": "LeGuinDylan_Casual_Outfit_NGP_D",
          "male_neutralfan_warm_017": "male_neutralfan_warm_019"}
_DONORS = []


def donor_face(name):
    """A real face (colour + normal) from the pack's generic player heads, picked by name (stable), for heads whose
    own face texture the pack does not hold. The heads share one UV layout."""
    if not _DONORS:
        for d in sorted(glob.glob(ap("Models/Players/Heads/gen_*/"))):
            who = os.path.basename(d.rstrip("\\/"))
            for s in sections_of(os.path.join(d, who + "_mesh.fbx")):
                if s["name"].startswith("head_lod0"):
                    col, nrm = s["textures"].get("colorTexture"), s["textures"].get("normalClampTexture")
                    if isinstance(col, str) and isinstance(nrm, str) and white_fraction(ap(col)) < 0.2:
                        _DONORS.append((ap(col), ap(nrm)))
    if not _DONORS:
        return None
    import zlib
    return _DONORS[zlib.crc32(name.encode()) % len(_DONORS)]


def plan_recipes(job, rig_of):
    jobs = []
    recipes = json.load(open(os.path.join(PACK, "_work", "assemble_jobs.json")))
    for r in recipes:
        g, name = r["group"], r["name"].split("/")[-1]
        if g in ("Players", "Referees"):
            continue  # players are dressed by plan_outfits; officials by hero_jobs (body and head apart)
        fbxs = [os.path.join(PACK, "Models" + p["fbx"].split("Models", 1)[1]) for p in r["parts"]]
        # completions for the pack's partial recipes: the hospital outfits have no legs (the character's own legs are in
        # the pack); one coach recipe has cap, headset and glasses but no face (a generic coach head goes under them)
        if name.startswith("JamisonHank_Hospital_Outfit_"):
            v = "gp_f" if name.endswith("GP_F") else "ngp_d"
            fbxs.append(ap("Models/StoryCharacters/Actors/jamisonhank/jamisonhank_legs_%s_mesh.fbx" % v))
        if name == "revisdarrelle_head":
            fbxs.append(ap("Models/Coaches/Named/3_b_n_01/3_b_n_01_head_mesh.fbx"))
        # the pack's torn right arm in two GP_F outfits: the same character's intact NGP_D arm part
        if name in ("CruiseColt_Casual_Outfit_GP_F", "LeGuinDylan_Casual_Outfit_GP_F"):
            fbxs = [f.replace("_arms_gp_f_mesh", "_arms_ngp_d_mesh").replace("_hands_gp_f_mesh", "_hands_ngp_d_mesh") for f in fbxs]
        fbxs = [f for f in fbxs if os.path.exists(f)]
        rig = RIGS.get(rig_of(fbxs[0]), "hero")
        parts = []
        for f in fbxs:
            p = build_part(f)
            if g in ("Crowd", "CrowdProps") or (g == "Sidelines" and rig in ("crowd", "sideplayer")):
                p["slots"] = {k: "Body" for k in p["slots"]}
            parts.append(p)
        parts = unique_slots(parts)
        donor = None
        for p in parts:
            for sec, slot in p["slots"].items():
                t = p["textures"].get(sec, {})
                col = t.get("colorTexture")
                if slot == "Head" and (not col or "flatgrey" in col or white_fraction(col) > 0.35):
                    donor = donor_face(name)
                    if donor:
                        t["colorTexture"], t["normalClampTexture"] = donor
                        t["_donor"] = True
        safe = re.sub(r"[^A-Za-z0-9_]", "_", name)
        if g == "PlayerHeads":
            kind, nm = "Head", "Head_" + safe
        elif g == "Coaches":
            kind, nm = ("CoachHead", "CoachHead_" + safe[:-5]) if name.endswith("_head") else ("CoachFull", "Coach_" + safe)
        elif g == "CoachBodies":
            kind, nm = "Coach", "CoachBody_" + safe
        elif g == "Sidelines":
            kind, nm = ("Crew" if rig == "crowd" else "SidePlayer" if rig == "sideplayer" else "Staff"), "Side_" + safe
        elif g == "Crowd":
            kind, nm = "Fan", "Fan_" + safe
        elif g == "CrowdProps":
            kind, nm = "FanProp", "FanProp_" + safe
        elif g == "StoryCharacters" and name.endswith("Geared_GP_F") or name.startswith("BullFrogsPlayer"):
            kind, nm = "Head", "Story_" + safe   # players in gear: the recipe is the head (the gear is the player kit)
        else:
            kind, nm = "Story", "Story_" + safe
        meta = {"source": name}
        if donor:
            meta["face_from"] = os.path.basename(donor[0])
        if kind == "CoachHead" and not any("Head" in p["slots"].values() for p in parts):
            meta["nohead"] = True  # the pack's recipe has the cap and headset but no face
        m = re.search(r"_(warm|cold|rain)", name.lower())
        if m:
            meta["weather"] = m.group(1)
        m = re.search(r"_([bhmtd])_(warm|cold)$|coachreferee_([bhm])_|_([bhm])$", name)
        if m:
            meta["build"] = next(x for x in m.groups() if x and x not in ("warm", "cold"))
        top = re.search(r"(poloshirt|windbreaker|rainjacket|mediavesttshirt|mediavestsweater)", name)
        if top:
            meta["top"] = top.group(1)
        meta["hat"] = any(re.search(r"_hat|visor|cap", os.path.basename(f).lower()) for f in fbxs)
        j = dict(job(nm, kind, parts, rig=rig, **meta))
        # torn skins (weights on the wrong bones, measured by hero_build's "far" share): weights from an intact sibling
        if name in REPAIR:
            donor = next((x for x in recipes if x["name"].split("/")[-1] == REPAIR[name]), None)
            if donor:
                j["weights_from"] = [os.path.join(PACK, "Models" + p["fbx"].split("Models", 1)[1]) for p in donor["parts"]]
                j["meta"]["weights_from"] = REPAIR[name]
        jobs.append(j)
    return jobs
