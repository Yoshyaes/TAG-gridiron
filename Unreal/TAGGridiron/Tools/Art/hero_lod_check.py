# Unreal Editor Python (headless). Checks the reduced LODs (Tools/Art/hero_lods.py) of every Story / Crowd / Coaches
# mesh against its LOD 0: each material slot's geometry in LOD 1-2 must sit where it sits in LOD 0. A re-import used to
# replace LOD 0 only, leaving LOD 1-2 with the old section to slot maps (far away a rebuilt character drew its jacket in
# the jeans' material); hero_import.py now rebuilds them. Small findings on LOD 2 are hair cards thinned by the
# reduction; a stale map moves a whole part (tens of cm).
#   UnrealEditor-Cmd.exe TAGGridiron.uproject -run=pythonscript -script="<this file>" -unattended -nullrhi
# Log lines "LODAUDIT ...".
import unreal
GS = unreal.GeometryScript_AssetUtils
MS = unreal.GeometryScript_MeshSelection
MQ = unreal.GeometryScript_MeshSelectionQueries
sub = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
def boxes(m, lod, n):
    dm = unreal.DynamicMesh()
    rl = unreal.GeometryScriptMeshReadLOD(lod_type=unreal.GeometryScriptLODType.RENDER_DATA, lod_index=lod)
    r = GS.copy_mesh_from_skeletal_mesh(m, dm, unreal.GeometryScriptCopyMeshFromAssetOptions(), rl)
    if isinstance(r, tuple): dm = next((x for x in r if isinstance(x, unreal.DynamicMesh)), dm)
    out = {}
    for mid in range(n):
        s = MS.select_mesh_elements_by_material_id(dm, mid, unreal.GeometryScriptMeshSelectionType.TRIANGLES)
        if isinstance(s, tuple): s = next(x for x in s if isinstance(x, unreal.GeometryScriptMeshSelection))
        b = MQ.get_mesh_selection_bounding_box(dm, s)
        if isinstance(b, tuple): b = next(x for x in b if isinstance(x, unreal.Box))
        if b.max.z - b.min.z > 0 or b.max.x - b.min.x > 0:
            out[mid] = ((b.min.x + b.max.x) / 2, (b.min.z + b.max.z) / 2, b.max.z - b.min.z)
    return out
bad = []; n_checked = 0
for d in ("Story", "Crowd", "Coaches"):
    for p in unreal.EditorAssetLibrary.list_assets("/Game/TAG/Hero/" + d, recursive=False, include_folder=False):
        p = p.split(".")[0]
        m = unreal.load_asset(p)
        if not isinstance(m, unreal.SkeletalMesh): continue
        nl = sub.get_lod_count(m)
        if nl < 2: continue
        n_checked += 1
        ns = len(m.get_editor_property("materials"))
        try:
            b0 = boxes(m, 0, ns)
            for lod in range(1, nl):
                bl = boxes(m, lod, ns)
                for mid, (cx, cz, h) in bl.items():
                    if mid in b0:
                        cx0, cz0, h0 = b0[mid]
                        if abs(cz - cz0) > 15 or abs(h - h0) > 0.5 * max(h0, 20):
                            bad.append("%s LOD%d slot %d moved z %.0f->%.0f h %.0f->%.0f" % (p.rsplit("/", 1)[1], lod, mid, cz0, cz, h0, h))
                    else:
                        bad.append("%s LOD%d slot %d has geometry not in LOD0" % (p.rsplit("/", 1)[1], lod, mid))
        except Exception as e:
            bad.append("%s error %r" % (p, e))
unreal.log("LODAUDIT checked %d meshes, %d findings" % (n_checked, len(bad)))
for b in bad[:60]:
    unreal.log("LODAUDIT " + b)
