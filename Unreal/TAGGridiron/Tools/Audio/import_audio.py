# Unreal Editor Python (headless). Imports the built sounds (Tools/Audio/build_*.py -> <out>/<group>/<Bank>/<take>.wav)
# as USoundWave assets under /Game/TAG/Audio/<group>/<Bank>/, and removes assets whose wav is gone.
#   UnrealEditor-Cmd.exe TAGGridiron.uproject -run=pythonscript -script="<this file>" -unattended -nosplash -nullrhi
# TAG_AUDIO_OUT = the build's output folder (default D:/TAGAudio/out). TAG_AUDIO_ONLY = comma list of name parts.
import glob
import os

import unreal

EAL = unreal.EditorAssetLibrary
SRC = os.environ.get("TAG_AUDIO_OUT", "D:/TAGAudio/out").replace("\\", "/")
ONLY = [s for s in os.environ.get("TAG_AUDIO_ONLY", "").split(",") if s]
DEST = "/Game/TAG/Audio"
LOOPS = ("CrowdCalm", "CrowdBuzz", "CrowdRoar", "CrowdNoise", "WxRain", "WxWind")


def main():
    wavs = sorted(glob.glob(SRC + "/**/*.wav", recursive=True))
    want = {}
    for w in wavs:
        rel = os.path.relpath(w, SRC).replace("\\", "/")[:-4]
        want[DEST + "/" + rel] = w
    tasks = []
    for path, w in want.items():
        if ONLY and not any(o in path for o in ONLY):
            continue
        # unchanged since the last import: skip
        if EAL.does_asset_exist(path):
            tag = EAL.get_metadata_tag(EAL.load_asset(path), "tag_src")
            if tag == "%d:%d" % (os.path.getsize(w), int(os.path.getmtime(w))):
                continue
        t = unreal.AssetImportTask()
        t.filename = w
        t.destination_path = path.rsplit("/", 1)[0]
        t.destination_name = path.rsplit("/", 1)[1]
        t.automated = True
        t.replace_existing = True
        t.save = False
        t.factory = unreal.SoundFactory()
        tasks.append((t, path, w))
    if tasks:
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t for t, _, _ in tasks])
    done = 0
    for t, path, w in tasks:
        if not EAL.does_asset_exist(path):
            unreal.log_error("TAG audio import FAILED " + path)
            continue
        s = EAL.load_asset(path)
        bank = path.rsplit("/", 2)[1]
        s.set_editor_property("looping", bank in LOOPS)
        EAL.set_metadata_tag(s, "tag_src", "%d:%d" % (os.path.getsize(w), int(os.path.getmtime(w))))
        EAL.save_loaded_asset(s, only_if_is_dirty=False)
        done += 1
    gone = 0
    if not ONLY and EAL.does_directory_exist(DEST):
        for a in EAL.list_assets(DEST, recursive=True, include_folder=False):
            p = a.split(".")[0]
            if p not in want:
                # (EditorAssetLibrary.delete_asset does nothing from the commandlet: the generated file itself goes)
                f = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir()), p[len("/Game/"):] + ".uasset")
                if not EAL.delete_asset(p) and os.path.exists(f):
                    os.remove(f)
                gone += 1
    unreal.log("TAG audio import: %d wavs, %d imported, %d removed" % (len(want), done, gone))


main()
