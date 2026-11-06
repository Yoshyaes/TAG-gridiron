@echo off
rem Copies Epic's template mannequin characters/animations (shipped with UE 5.8) into the project.
set "SRC=C:\Program Files\Epic Games\UE_5.8\Templates\TemplateResources\High\Characters\Content\Mannequins"
set "DST=%~dp0..\Content\Characters\Mannequins"
if not exist "%SRC%" (
  echo Could not find UE 5.8 template characters at "%SRC%"
  exit /b 1
)
robocopy "%SRC%" "%DST%" /E /NFL /NDL /NJH /NJS >nul
echo Mannequin assets copied to %DST%
exit /b 0
