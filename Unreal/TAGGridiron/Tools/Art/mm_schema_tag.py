# Unreal Editor Python (headless). A TAG PoseSearch schema for the TAG loop databases: a copy of the Game Animation
# Sample's PSS_Default with the trajectory channel weighted TAG_TRAJW times (default 3) against the pose channels, and
# every TAG database pointed at it. The sample's weights (trajectory 1.0 against feet positions 1.0 + velocities 0.3 +
# 0.3 + heading 0.1) suit its one actor's consistent poses; the pack's loops come from many men, and with pose
# features dominating the search picked a 5 m/s crouched run or a lateral kick slide for a 2 m/s forward drive
# (-TagMoveTest -TagMMTrace=67). TAG_TRAJW=0 removes the TAG schema again (databases back on PSS_Default).
#   TAG_TRAJW=3 UnrealEditor-Cmd.exe TAGGridiron.uproject -run=pythonscript -script="<this file>" -unattended -nosplash -nullrhi
import os

import unreal

EAL = unreal.EditorAssetLibrary
SRC = "/Game/Characters/UEFN_Mannequin/Animations/MotionMatchingData/Schemas/PSS_Default"
DST = "/Game/TAG/Anim/MM/PSS_TAG"
DBS = ["/Game/TAG/Anim/MM/PSD_TAG_%s_Loops" % g for g in ("Sprint", "Run", "Walk", "Crouch")]
w = float(os.environ.get("TAG_TRAJW", "3"))

if w <= 0:
    sch = unreal.load_asset(SRC)
else:
    if EAL.does_asset_exist(DST):
        sch = unreal.load_asset(DST)
    else:
        sch = EAL.duplicate_asset(SRC, DST)
    chans = sch.get_editor_property("channels")
    for ch in chans:
        if ch.get_class().get_name() == "PoseSearchFeatureChannel_Trajectory":
            ch.set_editor_property("weight", w)
    sch.set_editor_property("channels", chans)
    EAL.save_asset(DST, only_if_is_dirty=False)
for p in DBS:
    if EAL.does_asset_exist(p):
        db = unreal.load_asset(p)
        db.set_editor_property("schema", sch)
        EAL.save_asset(p, only_if_is_dirty=False)
        unreal.log("TAG_SCHEMA_SET %s -> %s" % (p.split("/")[-1], db.get_editor_property("schema").get_path_name()))
sch2 = unreal.load_asset(DST) if w > 0 else None
if sch2:
    for ch in sch2.get_editor_property("channels"):
        if ch.get_class().get_name() == "PoseSearchFeatureChannel_Trajectory":
            unreal.log("TAG_SCHEMA_SET trajectory weight=%s" % ch.get_editor_property("weight"))
