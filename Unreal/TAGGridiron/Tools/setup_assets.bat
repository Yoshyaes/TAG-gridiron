@echo off
rem One-time asset setup for TAG Gridiron (all content is engine-provided or generated locally).
set "UE=C:\Program Files\Epic Games\UE_5.8"
set "PROJDIR=%~dp0.."
rem 1) Epic template mannequin characters/animations
set "SRC=%UE%\Templates\TemplateResources\High\Characters\Content\Mannequins"
robocopy "%SRC%" "%PROJDIR%\Content\Characters\Mannequins" /E /NFL /NDL /NJH /NJS >nul
rem 2) procedural textures (turf, field markings, crowd)
python "%~dp0Art\gen_textures.py" "%PROJDIR%\RawArt" || (echo Python with Pillow+NumPy is required & exit /b 1)
rem 3) import textures + build materials inside Unreal (headless)
"%UE%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%PROJDIR%\TAGGridiron.uproject" -run=pythonscript "-script=%~dp0Art\ue_build_materials.py" -unattended -nosplash
echo Asset setup complete.
