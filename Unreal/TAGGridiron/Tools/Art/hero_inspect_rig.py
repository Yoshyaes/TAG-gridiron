# Unreal Editor Python (headless, read only): the first bones of the model pack meshes' reference skeletons
# (name, parent, local translation / rotation / scale). Saved/hero_inspect_rig.txt
import os

import unreal

OUT = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "hero_inspect_rig.txt")
L = []
for p in ("/Game/TAG/Hero/Rig/SKM_TAG_HeroRig", "/Game/TAG/Hero/Kits/Kit_d_skill", "/Game/TAG/Hero/Kits/Kit_h_line",
          "/Game/TAG/Hero/Heads/Head_abdullahameer_2402", "/Game/TAG/Hero/Rig/SKM_TAG_CrowdRig", "/Game/TAG/Hero/Rig/SKM_TAG_HeroRig_Skeleton"):
    a = unreal.load_asset(p)
    if not a:
        L.append("MISSING " + p)
        continue
    sk = a if isinstance(a, unreal.Skeleton) else a.get_editor_property("skeleton")
    pose = unreal.AnimPoseExtensions.get_reference_pose(sk) if isinstance(a, unreal.Skeleton) else None
    L.append("== %s (%s)" % (p, a.get_class().get_name()))
    if isinstance(a, unreal.SkeletalMesh):
        # the mesh's own reference pose
        try:
            names = [str(n) for n in unreal.AnimPoseExtensions.get_bone_names(unreal.AnimPoseExtensions.get_reference_pose(sk))]
        except Exception as e:
            names = []
            L.append("  names err %s" % e)
        L.append("  skeleton bones %d first %s" % (len(names), names[:6]))
        try:
            pose = unreal.SkeletalMeshEditorSubsystem if False else None
        except Exception:
            pass
    sp = unreal.AnimPoseExtensions.get_reference_pose(sk)
    for n in [str(x) for x in unreal.AnimPoseExtensions.get_bone_names(sp)][:5] + ["pelvis", "head"]:
        try:
            t = unreal.AnimPoseExtensions.get_bone_pose(sp, n, unreal.AnimPoseSpaces.LOCAL)
            w = unreal.AnimPoseExtensions.get_bone_pose(sp, n, unreal.AnimPoseSpaces.WORLD)
            L.append("  %-14s local t=%s r=%s s=%s | world t=%s s=%s" % (n, t.translation, t.rotation, t.scale3d, w.translation, w.scale3d))
        except Exception as e:
            L.append("  %s err %s" % (n, e))
open(OUT, "w").write("\n".join(L) + "\n")
