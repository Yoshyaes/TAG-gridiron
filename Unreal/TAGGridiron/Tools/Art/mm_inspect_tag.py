# Unreal Editor Python (headless, read only): list the animations in the TAG motion matching databases and their
# Dense sources, with each clip's root-motion flag and play length.
#   UnrealEditor-Cmd.exe TAGGridiron.uproject -run=pythonscript -script="<this file>" -unattended -nosplash -nullrhi
import unreal

EAL = unreal.EditorAssetLibrary
LIB = unreal.TagEditorLib
for p in ("/Game/Characters/UEFN_Mannequin/Animations/MotionMatchingData/Databases/Dense/PSD_Dense_Stand_Sprint_Loops",
          "/Game/Characters/UEFN_Mannequin/Animations/MotionMatchingData/Databases/Dense/PSD_Dense_Stand_Run_Loops",
          "/Game/TAG/Anim/MM/PSD_TAG_Sprint_Loops", "/Game/TAG/Anim/MM/PSD_TAG_Run_Loops", "/Game/TAG/Anim/MM/PSD_TAG_Walk_Loops",
          "/Game/TAG/Anim/MM/PSD_TAG_Crouch_Loops"):
    exists = EAL.does_asset_exist(p)
    db = unreal.load_asset(p) if exists else None
    names = list(LIB.database_anim_names(db)) if db else []
    unreal.log("TAG_MM_INSPECT %s exists=%s loaded=%s anims=%d %s" % (p.split("/")[-1], exists, db is not None, len(names), names[:40]))
for n in ("A_TAG_Loco_Sprint_Normal", "A_TAG_Loco_Line_KickSlide_L"):
    a = unreal.load_asset("/Game/TAG/Anim/Authored/" + n)
    unreal.log("TAG_MM_INSPECT clip %s loaded=%s rm=%s len=%s" % (n, a is not None, a.get_editor_property("enable_root_motion") if a else None, a.get_play_length() if a else None))
