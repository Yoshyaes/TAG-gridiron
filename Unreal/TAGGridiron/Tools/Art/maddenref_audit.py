# Unreal Editor Python. Guardrail audit for the private /Game/MaddenRef reference folder.
# Run headless:
#   UnrealEditor-Cmd.exe TAGGridiron.uproject -run=pythonscript -script="<this file>" -unattended -nosplash -nullrhi
# Writes JSON to env MADDENREF_AUDIT (default <project>/Saved/maddenref_audit.json).
#
# Fails when
#   1. any package outside /Game/MaddenRef references a package inside it
#   2. any file under Content/MaddenRef or the repo extracted folder is tracked by git
#   3. Content/MaddenRef or extracted is not ignored by git
#   4. DefaultGame.ini lacks the DirectoriesToNeverCook entry for /Game/MaddenRef
import json
import os
import subprocess

import unreal

ROOT = "/Game/MaddenRef"
PROJ = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
REPO = os.path.abspath(os.path.join(PROJ, "..", ".."))


def git(*args):
    p = subprocess.run(["git", "-C", REPO] + list(args), capture_output=True, text=True, timeout=120)
    return p.returncode, p.stdout.strip()


def run():
    problems = []
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    reg.search_all_assets(True)
    assets = reg.get_assets_by_path(ROOT, recursive=True)
    packages = sorted({str(a.package_name) for a in assets})
    opts = unreal.AssetRegistryDependencyOptions(include_soft_package_references=True, include_hard_package_references=True,
                                                 include_searchable_names=False, include_soft_management_references=True,
                                                 include_hard_management_references=True)
    outside_refs = []
    for pkg in packages:
        for ref in reg.get_referencers(pkg, opts) or []:
            ref = str(ref)
            if not ref.startswith(ROOT):
                outside_refs.append([ref, pkg])
    if outside_refs:
        problems.append("packages outside MaddenRef reference it: %d" % len(outside_refs))

    rc, tracked = git("ls-files", "--", "Unreal/TAGGridiron/Content/MaddenRef", "extracted")
    if tracked:
        problems.append("tracked files under MaddenRef or extracted: " + tracked[:500])
    for probe in ("Unreal/TAGGridiron/Content/MaddenRef/probe.uasset", "extracted/probe.fbx"):
        rc, _ = git("check-ignore", "-q", probe)
        if rc != 0:
            problems.append("not git ignored: " + probe)

    ini = open(os.path.join(PROJ, "Config", "DefaultGame.ini"), encoding="utf-8").read()
    if '+DirectoriesToNeverCook=(Path="/Game/MaddenRef")' not in ini:
        problems.append("DefaultGame.ini missing DirectoriesToNeverCook for /Game/MaddenRef")

    report = {"status": "pass" if not problems else "fail", "problems": problems,
              "maddenref_packages": len(packages), "outside_referencers": outside_refs[:200]}
    out = os.environ.get("MADDENREF_AUDIT") or os.path.join(PROJ, "Saved", "maddenref_audit.json")
    with open(out, "w") as fh:
        json.dump(report, fh, indent=1)
    unreal.log("MADDENREF_AUDIT " + report["status"] + " packages=%d problems=%d" % (len(packages), len(problems)))


run()
