@echo off
setlocal
cd /d "%~dp0"

if not exist "models" mkdir "models"

curl.exe -L --fail --output "models\LFM2.5-350M-Q8_0.gguf" ^
"https://huggingface.co/LiquidAI/LFM2.5-350M-GGUF/resolve/main/LFM2.5-350M-Q8_0.gguf"

if errorlevel 1 exit /b 1

echo Downloaded models\LFM2.5-350M-Q8_0.gguf
endlocal
