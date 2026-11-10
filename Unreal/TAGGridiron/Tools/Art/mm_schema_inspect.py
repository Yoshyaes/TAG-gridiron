# Unreal Editor Python (headless): the PoseSearch schema behind the TAG databases, channel by channel (recursing into
# channel groups) with weights, and the databases' cost biases. With TAG_CONT / TAG_LOOP set, writes those biases
# (continuing_pose_cost_bias / looping_cost_bias) to the four TAG databases first.
#   TAG_CONT=-0.2 UnrealEditor-Cmd.exe TAGGridiron.uproject -run=pythonscript -script="<this file>" -unattended -nosplash -nullrhi
import os

import unreal

EAL = unreal.EditorAssetLibrary
DBS = ["/Game/TAG/Anim/MM/PSD_TAG_%s_Loops" % g for g in ("Sprint", "Run", "Walk", "Crouch")]


def props(o, names):
    out = {}
    for n in names:
        try:
            v = o.get_editor_property(n)
            out[n] = str(v) if not isinstance(v, (int, float, bool, str)) else v
        except Exception:
            pass
    return out


CH_PROPS = ["weight", "bone", "sample_times", "sample_time_offset", "sample_role", "input_query_pose", "position_weight",
            "velocity_weight", "facing_weight", "samples", "use_character_space_velocities", "component_stripping",
            "origin_time_offset", "origin_bone", "offset", "normalization_group", "permutation_time_type"]


def dump(ch, depth):
    cls = ch.get_class().get_name()
    unreal.log("TAG_SCHEMA %s channel %s %s" % ("  " * depth, cls, props(ch, CH_PROPS)))
    try:
        for sub in ch.get_editor_property("sub_channels"):
            if sub:
                dump(sub, depth + 1)
    except Exception:
        pass


cont, loop = os.environ.get("TAG_CONT"), os.environ.get("TAG_LOOP")
for p in DBS:
    if not EAL.does_asset_exist(p):
        continue
    db = unreal.load_asset(p)
    if cont is not None or loop is not None:
        if cont is not None:
            db.set_editor_property("continuing_pose_cost_bias", float(cont))
        if loop is not None:
            db.set_editor_property("looping_cost_bias", float(loop))
        EAL.save_asset(p, only_if_is_dirty=False)
    sch = db.get_editor_property("schema")
    unreal.log("TAG_SCHEMA db %s schema=%s %s" % (p.split("/")[-1], sch.get_path_name() if sch else None,
               props(db, ["continuing_pose_cost_bias", "base_cost_bias", "looping_cost_bias", "pose_search_mode", "exclude_from_database_parameters"])))
    if sch and p == DBS[0]:
        unreal.log("TAG_SCHEMA  %s" % props(sch, ["skeleton", "sample_rate", "mirror_data_table", "data_preprocessor", "number_of_principal_components",
                                                   "add_data_padding", "inject_additional_debug_channels"]))
        for ch in sch.get_editor_property("channels"):
            dump(ch, 0)
