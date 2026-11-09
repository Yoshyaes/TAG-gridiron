# Unreal Editor Python (headless, read only). Exports the Athlete's animation skeleton mesh (SKM_UEFN_Mannequin)
# to FBX for the Blender keyframe pipeline (Tools/Anim). Writes outside Content; saves no assets.
#   UnrealEditor-Cmd.exe TAGGridiron.uproject -run=pythonscript -script="<this file>" -unattended -nosplash -nullrhi
import os

import unreal

PROJ = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
OUT = os.environ.get("TAG_RIG_FBX") or os.path.join(PROJ, "RawArt", "Anim", "UEFN_Rig_Idle.fbx")
os.makedirs(os.path.dirname(OUT), exist_ok=True)

# The skeletal mesh exporter asserts in commandlets (no MeshObject), so export an idle clip: it carries the exact
# SK_UEFN_Mannequin hierarchy and reference pose, which is all the Blender rig needs.
clip = unreal.load_asset("/Game/Characters/UEFN_Mannequin/Animations/Idle/M_Neutral_Stand_Idle_Loop")
task = unreal.AssetExportTask()
task.object = clip
task.filename = OUT
task.automated = True
task.replace_identical = True
task.prompt = False
opts = unreal.FbxExportOption()
opts.set_editor_property("ascii", False)
opts.set_editor_property("export_morph_targets", False)
opts.set_editor_property("level_of_detail", False)
task.options = opts
ok = unreal.Exporter.run_asset_export_task(task)
unreal.log("TAG_RIG_EXPORT %s %s" % ("ok" if ok and os.path.exists(OUT) else "FAILED", OUT))
