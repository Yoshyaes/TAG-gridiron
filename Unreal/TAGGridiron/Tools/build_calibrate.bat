@echo off
rem Builds the engine-independent C++ sim + headless tools with MSVC (Build Tools 2022).
rem   build\SimCalibrate.exe [games=40] [seed=7] [--scores]   -- port of tools/calibrate.ts
rem   build\SimTests.exe                                       -- port of tools/test.ts
setlocal
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
  echo vcvars64.bat not found at "%VCVARS%"
  exit /b 1
)
call "%VCVARS%" >nul 2>&1 || exit /b 1

cd /d "%~dp0"
if not exist build mkdir build
set "SIM=..\Source\TAGGridiron\Sim"
set FLAGS=/nologo /std:c++17 /O2 /EHsc /W4 /permissive- /utf-8 /I"%SIM%"

echo [1/3] Compiling Sim sources...
cl %FLAGS% /c "%SIM%\League.cpp" "%SIM%\Playbook.cpp" "%SIM%\PlaySim.cpp" "%SIM%\Game.cpp" "%SIM%\Stats.cpp" /Fobuild\ || exit /b 1
set "OBJS=build\League.obj build\Playbook.obj build\PlaySim.obj build\Game.obj build\Stats.obj"

echo [2/3] Building SimCalibrate.exe...
cl %FLAGS% SimCalibrate.cpp %OBJS% /Fobuild\ /Febuild\SimCalibrate.exe || exit /b 1

echo [3/3] Building SimTests.exe...
cl %FLAGS% SimTests.cpp %OBJS% /Fobuild\ /Febuild\SimTests.exe || exit /b 1

echo Build OK: %~dp0build\SimCalibrate.exe, %~dp0build\SimTests.exe
endlocal
