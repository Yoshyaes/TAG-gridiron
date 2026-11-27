# The body class of a motion matching capture and of a player, as the game has them (FAthleteAssets::CaptureClass and
# AAthlete::BodyClass in Source/TAGGridiron/Render/Athlete.cpp): keep the three the same.
# Used by Tools/Art/mm_style_sets_hero.py (which captures go in which class set) and Tools/Anim/mm_ab.py (the share of
# each class's frames on each class's captures in a -TagHlFull capture).
# The pack names its locomotion by the body it filmed (MG_BIGMAN_*, MG_NonBC_BigGuy_*, MG_Loco_Heavy_*, MG_LB_Med_*,
# MG_NonBC_Bruiser_*, MG_NonBC_Agile_*, MG_NonBC_WR_*, PrePlay_DB_*, KickReturn); the authored loops (A_TAG_Loco_*) are
# named for what they are, so they carry the class of the capture each was cut from (mocap_map.LOCO "src").
# Matching ignores case, as FString::Contains does.

FATIGUED = ["Fatigued"]
BIG = ["BIGMAN", "BigGuy", "Heavy", "Dline", "Run_Back_Big", "Line_DL_Jog"]
MED = ["LB_Med", "MG_LB_", "Bruiser", "Loco_Run_LB", "Loco_Jog_LB", "Loco_Run_Back", "Loco_Run_Side",
       "Line_BackDiag_Fast", "Line_BackDiag_Run", "Line_Side_Run", "Walk_Back_Slow", "Loco_Walk_Side",
       "Shuffle_LB", "Line_Back_Walk", "Line_Bounce", "Idle_LB"]
AGILE = ["Agile", "_WR_", "_DB_", "KickReturn", "Backpedal_To_Run", "Loco_Run_Diag"]
CLASSES = ("Big", "Med", "Agile")


def has(name, toks):
    low = name.lower()
    return any(t.lower() in low for t in toks)


def capture_class(name):
    """'Fatigued', 'Big', 'Med', 'Agile' or 'General' (no body in the name)."""
    if has(name, FATIGUED):
        return "Fatigued"
    if has(name, BIG):
        return "Big"
    if has(name, MED):
        return "Med"
    if has(name, AGILE):
        return "Agile"
    return "General"


def pack_class(src):
    """The same from a pack capture's own name only (the rule the authored names stand in for)."""
    if has(src, FATIGUED):
        return "Fatigued"
    if has(src, ["BIGMAN", "BigGuy", "Heavy", "Dline"]):
        return "Big"
    if has(src, ["LB_Med", "MG_LB_", "Bruiser"]):
        return "Med"
    if has(src, ["Agile", "_WR_", "_DB_", "KickReturn"]):
        return "Agile"
    return "General"


def body_class(pos, weight_lb=None):
    """A player's class by position (and build where it decides: backs, quarterbacks, kickers); None when the weight
    that decides it is not known."""
    pos = (pos or "").upper()
    if pos in ("OT", "OG", "C", "DE", "DT"):
        return "Big"
    if pos in ("LB", "TE", "FB"):
        return "Med"
    if pos in ("WR", "CB", "S"):
        return "Agile"
    if weight_lb is None:
        return None
    if pos == "RB":
        return "Med" if weight_lb >= 225 else "Agile"
    return "Med" if weight_lb >= 235 else "Agile"
