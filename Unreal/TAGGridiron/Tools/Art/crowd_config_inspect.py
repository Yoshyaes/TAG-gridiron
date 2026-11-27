# Unreal Editor Python (headless, read only): traits inside the MetaHuman Crowd Sample's Mass entity configs.
import json
import os

import unreal

OUT = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "crowd_config.json")
ROOT = "/MetaHumanCrowdContent"
rep = {"configs": {}, "errors": []}


def short(v, n=200):
    return str(v)[:n]


try:
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([ROOT], True)
    for p in ("/AI/DA_MHMassCrowdConfig", "/AI/DA_MHMassCrowdConfig_Base", "/AI/DA_MassPuppet",
              "/StarterKit_MigrateMe/Blueprint/MassPuppetConfig_ForActorBP"):
        a = unreal.load_asset(ROOT + p)
        if not a:
            rep["errors"].append("missing " + p)
            continue
        cfg = a.get_editor_property("config")
        info = {"class": a.get_class().get_name()}
        try:
            info["parent"] = short(cfg.get_editor_property("parent"))
        except Exception as e:
            info["parent"] = "?" + str(e)[:80]
        traits = []
        for t in cfg.get_editor_property("traits"):
            tr = {"class": t.get_class().get_name(), "props": {}}
            for name in dir(t):
                if name.startswith("_"):
                    continue
                try:
                    v = t.get_editor_property(name)
                except Exception:
                    continue
                if callable(v):
                    continue
                tr["props"][name] = short(v)
            traits.append(tr)
        info["traits"] = traits
        rep["configs"][p] = info
except Exception:
    import traceback
    rep["errors"].append(traceback.format_exc()[-1500:])
json.dump(rep, open(OUT, "w"), indent=1)
unreal.log("TAG_CROWD_CONFIG done errors=%d" % len(rep["errors"]))
