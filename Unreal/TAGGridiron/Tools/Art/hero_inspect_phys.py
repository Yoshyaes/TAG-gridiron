# Unreal Editor Python (headless, read only): the model pack ragdoll physics asset (bodies, the bones they sit on,
# constraints) and which character meshes carry it. Saved/hero_inspect_phys.txt
import os

import unreal

OUT = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "hero_inspect_phys.txt")
L = []
pa = unreal.load_asset("/Game/TAG/Hero/Rig/SKM_TAG_HeroRig_PhysicsAsset")
if not pa:
    L.append("MISSING physics asset")
else:
    bodies = pa.get_editor_property("skeletal_body_setups")
    L.append("bodies %d" % len(bodies))
    L.append("bones " + ", ".join(str(b.get_editor_property("bone_name")) for b in bodies))
    try:
        L.append("constraints %d" % len(pa.get_editor_property("constraint_setup")))
    except Exception as e:
        L.append("constraints n/a %s" % e)
for p in ("/Game/TAG/Hero/Kits/Kit_h_line", "/Game/TAG/Hero/Kits/Kit_t_skill", "/Game/TAG/Hero/Coaches/CoachBody_b_poloshirt", "/Game/TAG/Hero/Officials/Ref_warm"):
    m = unreal.load_asset(p)
    ph = m.get_editor_property("physics_asset") if m else None
    L.append("%s physics %s" % (p, ph.get_path_name() if ph else None))
open(OUT, "w").write("\n".join(L) + "\n")
