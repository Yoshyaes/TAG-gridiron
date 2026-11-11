# Unreal Editor Python (headless, read only). Reports the model pack character materials: master compile statistics,
# each sample mesh's slots and the instance / parent / textures on them. Saved/hero_inspect.txt
import os

import unreal

MEL = unreal.MaterialEditingLibrary
OUT = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "hero_inspect.txt")
L = []
for p in ("/Game/TAG/Hero/Materials/M_TAG_Hero", "/Game/TAG/Hero/Materials/M_TAG_HeroMasked"):
    m = unreal.load_asset(p)
    if not m:
        L.append("MISSING " + p)
        continue
    try:
        st = MEL.get_statistics(m)
        L.append("%s vs=%s ps=%s samplers=%s" % (p, st.num_vertex_shader_instructions, st.num_pixel_shader_instructions, st.num_samplers))
    except Exception as e:
        L.append("%s stats error %s" % (p, e))
    L.append("  expressions %d used_with_skeletal_mesh %s" % (MEL.get_num_material_expressions(m), m.get_editor_property("used_with_skeletal_mesh")))
for p in ("/Game/TAG/Hero/Kits/Kit_d_skill", "/Game/TAG/Hero/Helmets/Helmet_speedflex_speedflex2bar", "/Game/TAG/Hero/Heads/Head_abdullahameer_2402"):
    mesh = unreal.load_asset(p)
    if not mesh:
        L.append("MISSING " + p)
        continue
    L.append(p)
    for sm in mesh.get_editor_property("materials"):
        mi = sm.get_editor_property("material_interface")
        par = mi.get_editor_property("parent") if isinstance(mi, unreal.MaterialInstance) else None
        L.append("  slot %s -> %s parent %s" % (sm.get_editor_property("material_slot_name"), mi.get_path_name() if mi else None,
                                               par.get_path_name() if par else None))
        if isinstance(mi, unreal.MaterialInstance):
            for tp in mi.get_editor_property("texture_parameter_values"):
                L.append("     tex %s = %s" % (tp.get_editor_property("parameter_info").get_editor_property("name"),
                                              tp.get_editor_property("parameter_value").get_path_name() if tp.get_editor_property("parameter_value") else None))
open(OUT, "w").write("\n".join(L) + "\n")
