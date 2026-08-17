@echo off
setlocal

cd /d "%~dp0"

if not exist "models" mkdir "models"

echo Downloading Nemotron Speech model...

curl.exe -L --fail --progress-bar ^
  "https://huggingface.co/nvidia/nemotron-speech-streaming-en-0.6b/resolve/main/nemotron-speech-streaming-en-0.6b.q8_0.gguf" ^
  -o "models\nemotron-speech-streaming-en-0.6b.new.q8_0.gguf"

if errorlevel 1 (
    echo.
    echo ERROR: Model download failed.
    exit /b 1
)

echo.
echo Done.
echo Model saved to:
echo models\nemotron-speech-streaming-en-0.6b.new.q8_0.gguf