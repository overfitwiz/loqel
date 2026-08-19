@echo off
setlocal EnableExtensions

cd /d "%~dp0"

set "BUILD_DIR=build"
set "DIST_DIR=%CD%\dist"
set "MODELS_DIR=%DIST_DIR%\models"

if not exist "%BUILD_DIR%\CMakeCache.txt" (
    echo ERROR: The build directory is not configured.
    echo Run build-app.bat first.
    exit /b 1
)

set "CMAKE_EXE="

for /f "tokens=1,* delims==" %%A in ('findstr /b "CMAKE_COMMAND:INTERNAL=" "%BUILD_DIR%\CMakeCache.txt"') do set "CMAKE_EXE=%%B"

if not exist "%CMAKE_EXE%" (
    echo ERROR: The CMake used to configure the build could not be found.
    echo Run build-app.bat again.
    exit /b 1
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

if not exist "%VSWHERE%" (
    echo ERROR: Visual Studio not found.
    exit /b 1
)

set "VS_PATH="

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_PATH=%%i"

if not defined VS_PATH (
    echo ERROR: Visual Studio C++ Build Tools not found.
    exit /b 1
)

call "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat" >nul

if errorlevel 1 (
    echo ERROR: Failed to initialize MSVC.
    exit /b 1
)

echo Building the release...
"%CMAKE_EXE%" --build "%BUILD_DIR%" --config Release --parallel

if errorlevel 1 (
    echo ERROR: Release build failed.
    exit /b 1
)

if exist "%DIST_DIR%" rmdir /s /q "%DIST_DIR%"

echo Creating the portable release directory...
"%CMAKE_EXE%" --install "%BUILD_DIR%" --config Release --prefix "%DIST_DIR%"

if errorlevel 1 (
    echo ERROR: Release packaging failed.
    exit /b 1
)

REM Models are intentionally not copied into the release. Keep this directory
REM ready for the user to populate after packaging.
if not exist "%MODELS_DIR%" mkdir "%MODELS_DIR%"

if errorlevel 1 (
    echo ERROR: Could not create the models directory.
    exit /b 1
)

echo.
echo Release ready:
echo %DIST_DIR%
echo.
echo Copy model files to:
echo %MODELS_DIR%
echo.
echo Then run loqel.exe.

endlocal
exit /b 0
