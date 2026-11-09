@echo off
rem One-time asset setup for TAG Gridiron (all content is engine-provided or generated locally).
set "UE=C:\Program Files\Epic Games\UE_5.8"
set "PROJDIR=%~dp0.."
rem 1) Epic template mannequin characters/animations
set "SRC=%UE%\Templates\TemplateResources\High\Characters\Content\Mannequins"
robocopy "%SRC%" "%PROJDIR%\Content\Characters\Mannequins" /E /NFL /NDL /NJH /NJS >nul
rem 2) procedural textures (turf, field markings, crowd)
python "%~dp0Art\gen_textures.py" "%PROJDIR%\RawArt" || (echo Python with Pillow+NumPy is required & exit /b 1)
rem 2b) TAG uniform texture pack (digit atlas, jersey mesh knit, twill); imported by ue_build_mh_uniform.py
python "%~dp0Art\gen_uniform_textures.py" "%PROJDIR%\RawArt" || (echo Python with Pillow+NumPy is required & exit /b 1)
rem 3) import textures + build materials inside Unreal (headless)
"%UE%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%PROJDIR%\TAGGridiron.uproject" -run=pythonscript "-script=%~dp0Art\ue_build_materials.py" -unattended -nosplash
rem 3b) TAG football gear: procedural helmet and facemasks
"%UE%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%PROJDIR%\TAGGridiron.uproject" -run=pythonscript "-script=%~dp0Art\gear_build.py" -unattended -nosplash -nullrhi
rem 4) optional: Epic's free Game Animation Sample (download to <repo>\Assets\GASP) -> motion-captured athletes
set "GASP=%~dp0..\..\..\Assets\GASP\Content"
if exist "%GASP%\Characters\UEFN_Mannequin" (
  robocopy "%GASP%\Characters\UEFN_Mannequin" "%PROJDIR%\Content\Characters\UEFN_Mannequin" /E /MT:16 /NFL /NDL /NJH /NJS >nul
  rem animation curve-compression settings the GASP clips reference, and Epic's MetaHuman retarget rigs
  robocopy "%GASP%\Misc" "%PROJDIR%\Content\Misc" /E /NFL /NDL /NJH /NJS >nul
  robocopy "%GASP%\MetaHumans\Common\Common\Rigs" "%PROJDIR%\Content\MetaHumans\Common\Common\Rigs" /E /NFL /NDL /NJH /NJS >nul
  robocopy "%GASP%\MetaHumans\Common\Common\Mocap" "%PROJDIR%\Content\MetaHumans\Common\Common\Mocap" /E /NFL /NDL /NJH /NJS >nul
  robocopy "%GASP%\MetaHumans\Common\Female\Medium\NormalWeight\Body" "%PROJDIR%\Content\MetaHumans\Common\Female\Medium\NormalWeight\Body" /E /NFL /NDL /NJH /NJS >nul
  rem needs the C++ module built first (UTagEditorLib rebuilds the blend spaces' runtime data)
  "%UE%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%PROJDIR%\TAGGridiron.uproject" -run=pythonscript "-script=%~dp0Art\ue_build_anims.py" -unattended -nosplash
  rem football copies of the motion matching loop databases (sprint without acceleration transitions + curve loops, run + arcs)
  "%UE%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%PROJDIR%\TAGGridiron.uproject" -run=pythonscript "-script=%~dp0Art\mm_build_dbs.py" -unattended -nosplash -nullrhi
) else (
  echo Game Animation Sample not found at %GASP% - using template mannequin animations.
)
rem 5) optional: MetaHuman athletes. Create them once inside the editor (Epic sign-in required) with
rem    py "<project>/Tools/Art/mh_create.py"   then this step builds the retargeter + uniform material.
if exist "%PROJDIR%\Content\TAG\MetaHumans\Unpacked\TAG_Skill" (
  "%UE%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%PROJDIR%\TAGGridiron.uproject" -run=pythonscript "-script=%~dp0Art\mh_retarget.py" -unattended -nosplash
  "%UE%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%PROJDIR%\TAGGridiron.uproject" -run=pythonscript "-script=%~dp0Art\ue_build_mh_uniform.py" -unattended -nosplash
)
rem 6) optional: MetaHuman Crowd Sample spectators (Fab; Plugins\MetaHumanCrowdContent junction to the engine install).
rem    Fan config without the walking traits, our cheer retargeted to the crowd skeleton, then a TAG rebuild of the
rem    crowd collection with the cheer baked as sequence 2 for distant instanced fans (~10 min; needs ~40 GB commit).
if exist "%PROJDIR%\Plugins\MetaHumanCrowdContent" (
  "%UE%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%PROJDIR%\TAGGridiron.uproject" -run=pythonscript "-script=%~dp0Art\crowd_fan_config.py" -unattended -nosplash -nullrhi
  "%UE%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%PROJDIR%\TAGGridiron.uproject" -run=pythonscript "-script=%~dp0Art\crowd_retarget.py" -unattended -nosplash
  "%UE%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%PROJDIR%\TAGGridiron.uproject" -run=pythonscript "-script=%~dp0Art\crowd_rebuild.py" -unattended -nosplash "-ini:Engine:[ConsoleVariables]:Editor.AsyncAssetCompilationMaxMemoryUsage=8,Editor.AsyncAssetCompilationMemoryPerCore=2"
)
echo Asset setup complete.
