# Unreal Editor Python (headless): re-import the three textures a team's name or mark is drawn into, over their assets,
# with the settings their own pipelines give them - after a team is renamed or its mark redrawn (gen_team_logos.py,
# gen_turf_textures.py logos words), without rebuilding the hero masters (hero_import.py TAG_HERO_MATS=1) or the turf
# materials (ue_build_turf.py):
#   /Game/TAG/Hero/Textures/Shared/T_T_TAG_TeamLogos_54723_L   the helmet atlas   (hero_import.py texture(), kind linear)
#   /Game/TAG/Textures/T_TagFieldLogos                          the midfield marks (ue_build_turf.py import_tex, linear)
#   /Game/TAG/Textures/T_TagFieldWords                          the end-zone words (ue_build_turf.py import_tex, gray)
#   UnrealEditor-Cmd.exe <full path>/TAGGridiron.uproject -run=pythonscript -script="<full path to this file>" -unattended -nosplash -nullrhi
# (The UI marks, Content/Slate/TeamLogos/<ABBR>.png, are read as files at run time: gen_ui_team_logos.py is enough.)
import os

import unreal

EAL = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
PROJ = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
RAW = os.path.join(PROJ, "RawArt")


def reimport(src, folder, name, settings):
    if not EAL.does_asset_exist(folder + "/" + name):
        unreal.log_error("TAG marks: %s/%s does not exist (run its pipeline first)" % (folder, name))
        return False
    task = unreal.AssetImportTask()
    task.filename = src
    task.destination_path = folder
    task.destination_name = name
    task.replace_existing = True
    task.automated = True
    task.save = False
    tools.import_asset_tasks([task])
    t = unreal.load_asset(folder + "/" + name)
    if t is None:
        unreal.log_error("TAG marks: import failed " + src)
        return False
    for k, v in settings:
        t.set_editor_property(k, v)
    EAL.save_loaded_asset(t, only_if_is_dirty=False)
    unreal.log("TAG marks: re-imported %s/%s" % (folder, name))
    return True


ok = reimport(os.path.join(RAW, "Hero", "Textures", "T_TAG_TeamLogos.png"), "/Game/TAG/Hero/Textures/Shared", "T_T_TAG_TeamLogos_54723_L",
              [("srgb", False), ("lod_group", unreal.TextureGroup.TEXTUREGROUP_CHARACTER_SPECULAR), ("max_texture_size", 2048)])
ok &= reimport(os.path.join(RAW, "T_TagFieldLogos.png"), "/Game/TAG/Textures", "T_TagFieldLogos", [("srgb", False)])
ok &= reimport(os.path.join(RAW, "T_TagFieldWords.png"), "/Game/TAG/Textures", "T_TagFieldWords",
               [("compression_settings", unreal.TextureCompressionSettings.TC_GRAYSCALE), ("srgb", False)])
unreal.log("TAG marks re-import %s" % ("OK" if ok else "FAILED"))
