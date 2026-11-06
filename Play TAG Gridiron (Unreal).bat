@echo off
title TAG Gridiron (Unreal Engine 5.8)
set "UE=C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
set "PROJ=%~dp0Unreal\TAGGridiron\TAGGridiron.uproject"
if not exist "%~dp0Unreal\TAGGridiron\Content\TAG\Materials" call "%~dp0Unreal\TAGGridiron\Tools\setup_assets.bat"
start "" "%UE%" "%PROJ%" -game -windowed -ResX=1920 -ResY=1080
