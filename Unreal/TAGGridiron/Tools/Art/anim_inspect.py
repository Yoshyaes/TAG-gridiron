# Read-only: inspect the TAG blend space internals (sample validity, runtime data, API).
import os
import unreal

OUT = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "tag_anim_inspect.txt")
L = []
bs = unreal.load_asset("/Game/TAG/Anim/BS_TAG_Locomotion")
L.append("methods: " + ", ".join(m for m in dir(bs) if not m.startswith("_") and m not in dir(unreal.Object)))
s0 = bs.get_editor_property("sample_data")[0]
L.append("sample fields: " + ", ".join(m for m in dir(s0) if not m.startswith("_")))
for name in ("is_valid", "b_is_valid", "cached_marker_data_update_counter", "animation"):
    try:
        L.append("  %s = %s" % (name, s0.get_editor_property(name)))
    except Exception as e:
        L.append("  %s err %s" % (name, e))
for prop in ("interpolation_param", "blend_space_data", "grid_samples", "axis_to_scale_animation", "b_interpolate_using_grid", "interpolate_using_grid",
             "preferred_triangulation_direction", "target_weight_interpolation_speed_per_sec", "notify_trigger_mode", "is_additive",
             "skeleton", "b_allow_mesh_space_blending", "contains_rotation_offset_mesh_space_samples"):
    try:
        v = bs.get_editor_property(prop)
        L.append("prop %s = %s" % (prop, str(v)[:300]))
    except Exception as e:
        L.append("prop %s n/a" % prop)
with open(OUT, "w", encoding="utf8") as f:
    f.write("\n".join(L) + "\n")
