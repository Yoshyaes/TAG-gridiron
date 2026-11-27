@echo off
rem Installs the broadcast booth's speech service (Tools\Booth\.venv): CUDA PyTorch, Chatterbox, Kokoro, faster-whisper.
rem Needs Python 3.11 and an NVIDIA GPU for real-time voices (CPU works, slowly). The game starts the service itself
rem (ATagBooth) and keeps every line it synthesizes in Saved\BoothVoice, so a line is only ever made once.
setlocal
cd /d "%~dp0"
set "PY=%LOCALAPPDATA%\Programs\Python\Python311\python.exe"
if not exist "%PY%" set "PY=py -3.11"
if not exist .venv (
  %PY% -m venv .venv || exit /b 1
)
.venv\Scripts\python.exe -m pip install --upgrade pip || exit /b 1
.venv\Scripts\python.exe -m pip install torch==2.6.0 torchaudio==2.6.0 --index-url https://download.pytorch.org/whl/cu124 || exit /b 1
.venv\Scripts\python.exe -m pip install chatterbox-tts kokoro soundfile faster-whisper scipy || exit /b 1
echo Booth voices installed. First game: the models download (~3 GB) and the generic calls are synthesized in the background.
endlocal
