@echo off
rem Cross-language verification: the TypeScript sim (src/sim) vs the C++ port, compared BIT-FOR-BIT.
rem Both sides run with identical deterministic reference math (refmath_setup.ts / RefMath.cpp), which
rem removes the only legitimate source of divergence (last-ulp differences between V8's and the CRT's
rem exp/log/sin/cos/atan2). Any remaining difference is a porting bug.
rem   1) gamelog:  the calibrate.ts game sequence, every PlayLog line (exact clock/toGo/ballOn bits)
rem   2) scenario: every play x defense x {AI, user offense, user defense}, exact final entity/ball state
rem Usage: verify_vs_ts.bat [games=100]      (needs node/npx at the repo root; does not modify src/)
setlocal
set "GAMES=%~1"
if "%GAMES%"=="" set GAMES=100
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
call "%VCVARS%" >nul 2>&1 || exit /b 1

cd /d "%~dp0"
if not exist build mkdir build
set "SIM=..\..\Source\TAGGridiron\Sim"
set FLAGS=/nologo /std:c++17 /O2 /EHsc /utf-8

echo Building reference-math C++ dumpers...
cl %FLAGS% /c RefMath.cpp /Fobuild\ || exit /b 1
cl %FLAGS% /I"%SIM%" /FI"%~dp0RefMathFI.h" /c "%SIM%\League.cpp" "%SIM%\Playbook.cpp" "%SIM%\PlaySim.cpp" "%SIM%\Game.cpp" "%SIM%\Stats.cpp" gamelog_dump.cpp scenario_dump.cpp /Fobuild\ || exit /b 1
set "OBJS=build\RefMath.obj build\League.obj build\Playbook.obj build\PlaySim.obj build\Game.obj build\Stats.obj"
link /nologo %OBJS% build\gamelog_dump.obj /OUT:build\gamelog_dump.exe || exit /b 1
link /nologo %OBJS% build\scenario_dump.obj /OUT:build\scenario_dump.exe || exit /b 1

echo Running C++ (%GAMES% games + scenarios)...
build\gamelog_dump.exe %GAMES% > build\gamelog_cpp.txt || exit /b 1
build\scenario_dump.exe > build\scenario_cpp.txt || exit /b 1

echo Running TypeScript (slow: ~1s per game)...
pushd "%~dp0..\..\..\.."
call npx tsx Unreal\TAGGridiron\Tools\Verify\gamelog_dump.ts %GAMES% > "%~dp0build\gamelog_ts.txt" || (popd & exit /b 1)
call npx tsx Unreal\TAGGridiron\Tools\Verify\scenario_dump.ts > "%~dp0build\scenario_ts.txt" || (popd & exit /b 1)
popd

set FAIL=0
fc /b build\gamelog_ts.txt build\gamelog_cpp.txt >nul && (echo gamelog : IDENTICAL) || (echo gamelog : DIFFERENT - compare build\gamelog_ts.txt build\gamelog_cpp.txt & set FAIL=1)
fc /b build\scenario_ts.txt build\scenario_cpp.txt >nul && (echo scenario: IDENTICAL) || (echo scenario: DIFFERENT - compare build\scenario_ts.txt build\scenario_cpp.txt & set FAIL=1)
exit /b %FAIL%
