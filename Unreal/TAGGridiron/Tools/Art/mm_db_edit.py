# Unreal Editor Python (headless). Edit one TAG PoseSearch database in place (bisecting what breaks a search):
#   TAG_DB=PSD_TAG_Crouch_Loops TAG_REMOVE=A_TAG_Loco,Shuffle TAG_ADD=Loco_Line_KickSlide_L,Loco_Line_Shuffle_Jog
#   UnrealEditor-Cmd.exe TAGGridiron.uproject -run=pythonscript -script="<this file>" -unattended -nosplash -nullrhi
# TAG_REMOVE: name substrings to drop; TAG_ADD: locomotion clip names (without A_TAG_) to add. Prints the result.
import os

import unreal

EAL = unreal.EditorAssetLibrary
LIB = unreal.TagEditorLib
name = os.environ.get("TAG_DB", "PSD_TAG_Crouch_Loops")
path = "/Game/TAG/Anim/MM/" + name
db = unreal.load_asset(path)
removed = sum(LIB.database_remove_anims_containing(db, s) for s in os.environ.get("TAG_REMOVE", "").split(",") if s)
added = []
for n in [s for s in os.environ.get("TAG_ADD", "").split(",") if s]:
    a = unreal.load_asset("/Game/TAG/Anim/Authored/A_TAG_" + n)
    if a and LIB.database_add_anim(db, a):
        added.append(n)
EAL.save_asset(path, only_if_is_dirty=False)
unreal.log("TAG_MM_EDIT %s removed %d added %s now %s" % (name, removed, added, list(LIB.database_anim_names(db))))
