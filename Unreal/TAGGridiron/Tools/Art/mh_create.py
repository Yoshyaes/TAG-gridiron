# Run INSIDE the open Unreal Editor (Output Log "Cmd" box):
#   py "C:/Users/soon2/Documents/Madden TAG/Unreal/TAGGridiron/Tools/Art/mh_create.py"
# Creates three MetaHuman body types for TAG Gridiron, requests textures + full rig from
# Epic's MetaHuman service (non-blocking), and assembles each one when it becomes buildable.
# Everything runs from an editor tick so the editor stays responsive.
# Progress is written to <Project>/Saved/tag_mh.log
import os
import time
import traceback

import unreal

SAVED = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir())
LOG = os.path.join(SAVED, "tag_mh.log")
FOLDER = "/Game/TAG/MetaHumans"

# Target body measurements (cm, circumferences) by position group - typical NFL builds.
#   lineman ~6'5" 315 lb, skill (WR/RB/DB) ~6'1" 205 lb, big (LB/TE/QB) ~6'3" 245 lb
BODIES = [
    ("TAG_Lineman", {"height": 196, "chest": 132, "waist": 118, "hip": 124, "neck": 51, "thigh": 76, "calf": 48, "across shoulder": 50}),
    ("TAG_Skill", {"height": 185, "chest": 104, "waist": 81, "hip": 98, "neck": 40, "thigh": 61, "calf": 40, "across shoulder": 47}),
    ("TAG_Big", {"height": 191, "chest": 118, "waist": 92, "hip": 108, "neck": 45, "thigh": 68, "calf": 43, "across shoulder": 49}),
]
PER_CHARACTER_TIMEOUT = 20 * 60
# `py ".../mh_create.py" retex` : bodies are already shaped + rigged; only re-fetch textures and re-assemble.
import sys
RETEX = any(a.lower() == "retex" for a in sys.argv[1:])


def log(*a):
    line = time.strftime("%H:%M:%S ") + " ".join(str(x) for x in a)
    with open(LOG, "a", encoding="utf8") as f:
        f.write(line + "\n")
    unreal.log("TAG_MH " + line)


def pick_enum(name, prefs):
    e = getattr(unreal, name, None)
    if not e:
        return None
    vals = [m for m in dir(e) if m.isupper()]
    for p in prefs:
        for v in vals:
            if p in v:
                return getattr(e, v)
    return None


open(LOG, "w").close()
sub = unreal.get_editor_subsystem(unreal.MetaHumanCharacterEditorSubsystem)
tools = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
if not EAL.does_directory_exist(FOLDER):
    EAL.make_directory(FOLDER)


def shape_body(ch, spec):
    cons = list(sub.get_body_constraints(ch))
    changed = []
    for i, c in enumerate(cons):
        try:
            nm = str(c.get_editor_property("name")).lower()
            target = spec.get(nm)  # exact constraint names only
            if target is None:
                c.set_editor_property("is_active", False)
            else:
                lo = c.get_editor_property("min_measurement")
                hi = c.get_editor_property("max_measurement")
                val = max(lo, min(hi, float(target)))
                c.set_editor_property("target_measurement", val)
                c.set_editor_property("is_active", True)
                changed.append("%s=%.0f" % (nm, val))
            cons[i] = c
        except Exception as e:
            log("  constraint error", e)
    sub.set_body_constraints(ch, cons)
    try:
        sub.commit_body_state(ch)
    except Exception as e:
        log("  commit_body_state:", e)
    log("  body:", ", ".join(changed))


def nonblocking(params):
    for prop, val in (("blocking", False), ("report_progress", True)):
        try:
            params.set_editor_property(prop, val)
        except Exception:
            pass
    return params


def build_params():
    p = unreal.MetaHumanCharacterEditorBuildParameters()
    pt = pick_enum("MetaHumanDefaultPipelineType", ["OPTIMIZED"])
    q = pick_enum("MetaHumanQualityLevel", ["HIGH", "MEDIUM"])
    if pt is not None:
        p.set_editor_property("pipeline_type", pt)
    if q is not None:
        p.set_editor_property("pipeline_quality", q)
    return p


S = {"idx": 0, "phase": "start", "t": time.time(), "last": 0.0, "lastlog": 0.0, "ch": None, "handle": None, "results": {}}


def rig_state(ch):
    """'rigged' / 'pending' / 'unrigged' / None if this engine build has no query for it."""
    fn = getattr(sub, "get_rigging_state", None)
    if fn is None:
        return None
    try:
        s = str(fn(ch)).upper()
    except Exception:
        return None
    if "PENDING" in s:
        return "pending"
    if "UNRIGGED" in s:
        return "unrigged"
    if "RIGGED" in s:
        return "rigged"
    return None


def request_textures(ch):
    sub.request_texture_sources(ch, nonblocking(unreal.MetaHumanCharacterTextureRequestParams()))
    log("  textures requested from the MetaHuman service (non-blocking)")


def start_character(name, spec):
    path = FOLDER + "/" + name
    ch = unreal.load_asset(path) if EAL.does_asset_exist(path) else tools.create_asset(name, FOLDER, unreal.MetaHumanCharacter, unreal.MetaHumanCharacterFactoryNew())
    log(name, "asset ready, rig state:", rig_state(ch))
    if not sub.is_object_added_for_editing(ch):
        sub.try_add_object_to_edit(ch)
    if RETEX or rig_state(ch) == "rigged":
        log("  already rigged - keeping body + rig, requesting textures")
        request_textures(ch)
        return ch, "tex"
    shape_body(ch, spec)
    rp = nonblocking(unreal.MetaHumanCharacterAutoRiggingRequestParams())
    full = pick_enum("MetaHumanRigType", ["BLEND_SHAPES"])
    if full is not None:
        rp.set_editor_property("rig_type", full)
    sub.request_auto_rigging(ch, rp)
    log("  full rig requested from the MetaHuman service (non-blocking, ~8-10 min)")
    return ch, "rig"


def finish_character(name, result):
    S["results"][name] = result
    try:
        EAL.save_loaded_asset(S["ch"])
    except Exception:
        pass
    rm = getattr(sub, "remove_object_to_edit", None)
    if rm is not None and S["ch"] is not None:
        try:
            rm(S["ch"])  # release it so it can be opened in MetaHuman Creator
        except Exception:
            pass
    S["idx"] += 1
    S["phase"] = "start"


def tick(dt):
    now = time.time()
    if now - S["last"] < 3.0:
        return
    S["last"] = now
    try:
        if S["idx"] >= len(BODIES):
            log("ALL DONE", S["results"])
            unreal.unregister_slate_post_tick_callback(S["handle"])
            return
        name, spec = BODIES[S["idx"]]
        if S["phase"] == "busy":
            return  # asset loading can pump Slate and re-enter this tick
        if S["phase"] == "start":
            S["phase"] = "busy"
            S["ch"], nxt = start_character(name, spec)
            S["phase"] = nxt
            S["t"] = time.time()
            return
        waited = now - S["t"]
        if waited > PER_CHARACTER_TIMEOUT:
            log(name, "TIMED OUT in phase", S["phase"])
            sub.can_build_meta_human(S["ch"], True)  # logs the reason to the Output Log
            finish_character(name, "timeout")
            return
        if S["phase"] == "rig":
            st = rig_state(S["ch"])
            # Without a rig-state query, give the service a fixed window before asking for textures.
            if st == "rigged" or (st is None and waited > 600):
                log(name, "rig ready after %.0fs" % waited)
                S["phase"] = "busy"
                request_textures(S["ch"])
                S["phase"] = "tex"
                S["t"] = time.time()
                return
        elif S["phase"] == "tex" and sub.can_build_meta_human(S["ch"], False):
            log(name, "textures + rig ready -> assembling")
            S["phase"] = "busy"
            sub.build_meta_human(S["ch"], build_params())
            log(name, "ASSEMBLED")
            finish_character(name, "assembled")
            return
        if now - S["lastlog"] > 30:
            S["lastlog"] = now
            log(name, "waiting on MetaHuman service (%s)... %.0fs" % (S["phase"], waited))
    except Exception:
        log("ERROR", traceback.format_exc())
        finish_character(BODIES[S["idx"]][0], "error")


# replace any previous run of this script in the same editor session
_prev = getattr(unreal, "_tag_mh_handle", None)
if _prev is not None:
    try:
        unreal.unregister_slate_post_tick_callback(_prev)
    except Exception:
        pass
S["handle"] = unreal.register_slate_post_tick_callback(tick)
unreal._tag_mh_handle = S["handle"]
log("subsystem API:", [m for m in dir(sub) if not m.startswith("_") and any(k in m for k in ("rig", "textur", "build", "edit"))])
log("started: building", [b[0] for b in BODIES], "one at a time; editor stays responsive")
