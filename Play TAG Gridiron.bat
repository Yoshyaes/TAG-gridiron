@echo off
title TAG Gridiron
cd /d "%~dp0"
if not exist node_modules (
  echo Installing dependencies...
  call npm install
)
echo Building TAG Gridiron...
call npm run build
if errorlevel 1 (
  echo Build failed.
  pause
  exit /b 1
)
echo Starting game server on http://localhost:4173 ...
start "" cmd /c "timeout /t 2 >nul & start http://localhost:4173"
call npx vite preview --port 4173 --strictPort
