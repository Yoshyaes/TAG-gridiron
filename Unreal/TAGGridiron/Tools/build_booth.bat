@echo off
rem Builds the headless broadcast booth (BoothScript.exe) against the engine-independent Sim, with MSVC (Build Tools 2022).
rem   build\BoothScript.exe [seed] [--hold] [--quiet] [--games=N] [--jsonl=path]
setlocal
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
  echo vcvars64.bat not found at "%VCVARS%"
  exit /b 1
)
call "%VCVARS%" >nul 2>&1 || exit /b 1
cd /d "%~dp0"
if not exist build\booth mkdir build\booth
set "SRC=..\Source\TAGGridiron"
set "SIM=%SRC%\Sim"
set FLAGS=/nologo /std:c++17 /O2 /EHsc /W4 /permissive- /utf-8 /I"%SIM%" /I"%SRC%"
cl %FLAGS% /c "%SIM%\League.cpp" "%SIM%\Playbook.cpp" "%SIM%\PlaySim.cpp" "%SIM%\Game.cpp" "%SIM%\Stats.cpp" "%SRC%\Booth\Booth.cpp" "%SRC%\Booth\BoothSpeech.cpp" "%SRC%\Booth\BoothWriter.cpp" /Fobuild\booth\ || exit /b 1
cl %FLAGS% BoothScript.cpp build\booth\League.obj build\booth\Playbook.obj build\booth\PlaySim.obj build\booth\Game.obj build\booth\Stats.obj build\booth\Booth.obj build\booth\BoothSpeech.obj build\booth\BoothWriter.obj /Fobuild\booth\ /Febuild\BoothScript.exe || exit /b 1
echo Build OK: %~dp0build\BoothScript.exe
endlocal
