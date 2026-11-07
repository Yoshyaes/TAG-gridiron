# Read-only: report skeletons / components of the assembled TAG MetaHumans.
import os
import unreal

OUT = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "tag_mh_inspect.txt")
lines = []


def w(*a):
    lines.append(" ".join(str(x) for x in a))


for n in ("TAG_Lineman", "TAG_Skill", "TAG_Big"):
    base = "/Game/TAG/MetaHumans/Unpacked/%s/" % n
    for part in ("Body/SKM_%s_BodyMesh" % n, "Face/SKM_%s_FaceMesh" % n):
        m = unreal.load_asset(base + part)
        if not m:
            w(n, part, "MISSING")
            continue
        sk = m.get_editor_property("skeleton")
        w(n, part, "skeleton=", sk.get_path_name() if sk else None,
          "phys=", m.get_editor_property("physics_asset").get_path_name() if m.get_editor_property("physics_asset") else None)
        try:
            bnds = m.get_bounds()
            w("   bounds extent", bnds.box_extent, "origin", bnds.origin)
        except Exception as e:
            w("   bounds err", e)
        try:
            mats = [s.get_editor_property("material_interface").get_path_name() if s.get_editor_property("material_interface") else None for s in m.get_editor_property("materials")]
            w("   materials", mats)
        except Exception as e:
            w("   mats err", e)
        if part.startswith("Body"):
            try:
                ref = unreal.SkeletalMeshEditorSubsystem if False else None
                names = [str(x) for x in unreal.SkeletalMeshEditorSubsystem.get_bone_names(m)] if hasattr(unreal.SkeletalMeshEditorSubsystem, "get_bone_names") else None
                w("   bones", len(names) if names else None, names[:40] if names else None)
            except Exception as e:
                w("   bones err", e)
    bp = unreal.load_asset(base + "BP_" + n)
    if bp:
        try:
            gen = unreal.EditorAssetLibrary.load_blueprint_class(base + "BP_" + n)
            cdo = unreal.get_default_object(gen)
            comps = cdo.get_components_by_class(unreal.ActorComponent)
            w("   BP components:", [(c.get_name(), c.get_class().get_name()) for c in comps])
        except Exception as e:
            w("   bp err", e)

for p in ("/Game/Characters/UEFN_Mannequin/Rigs/IK_UEFN_Mannequin",):
    w(p, "exists" if unreal.EditorAssetLibrary.does_asset_exist(p) else "NOT IN PROJECT")
w("retarget classes:", [c for c in dir(unreal) if "Retarget" in c][:60])
w("IKRig classes:", [c for c in dir(unreal) if c.startswith("IKRig")][:40])
with open(OUT, "w", encoding="utf8") as f:
    f.write("\n".join(lines) + "\n")
