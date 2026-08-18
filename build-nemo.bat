@echo off
setlocal EnableExtensions

REM ============================================================
REM Paths
REM ============================================================

cd /d "%~dp0"

set "ROOT=%~dp0"
set "NEMO_DIR=%ROOT%NeMo-Speech.cpp"
set "BUILD_DIR=%NEMO_DIR%\build-cpu-min"
set "INSTALL_DIR=%NEMO_DIR%\install-cpu-min"

set "TRIPLET=x64-windows-static-md"

echo.
echo ============================================================
echo  NeMo-Speech.cpp CPU Build
echo ============================================================
echo.

REM ============================================================
REM Check source
REM ============================================================

if not exist "%NEMO_DIR%\CMakeLists.txt" (
    echo ERROR: NeMo-Speech.cpp not found:
    echo %NEMO_DIR%
    exit /b 1
)

REM ============================================================
REM Find vcpkg
REM
REM Priority:
REM   1. Existing VCPKG_ROOT environment variable
REM   2. C:\vcpkg
REM ============================================================

set "VCPKG_DIR="

if defined VCPKG_ROOT (
    if exist "%VCPKG_ROOT%\vcpkg.exe" (
        set "VCPKG_DIR=%VCPKG_ROOT%"
    )
)

if not defined VCPKG_DIR (
    if exist "C:\vcpkg\vcpkg.exe" (
        set "VCPKG_DIR=C:\vcpkg"
    )
)

if not defined VCPKG_DIR (
    echo ERROR: vcpkg not found.
    echo.
    echo Either:
    echo   1. Install vcpkg at C:\vcpkg
    echo.
    echo or:
    echo.
    echo   2. Set VCPKG_ROOT to your vcpkg directory.
    echo.
    echo Example:
    echo   set VCPKG_ROOT=C:\dev\vcpkg
    echo.
    exit /b 1
)

if not exist "%VCPKG_DIR%\scripts\buildsystems\vcpkg.cmake" (
    echo ERROR: vcpkg CMake toolchain not found:
    echo %VCPKG_DIR%\scripts\buildsystems\vcpkg.cmake
    exit /b 1
)

echo vcpkg:
echo %VCPKG_DIR%\vcpkg.exe
echo.

REM ============================================================
REM Find Visual Studio
REM ============================================================

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

if not exist "%VSWHERE%" (
    echo ERROR: vswhere.exe not found.
    echo.
    echo Visual Studio or Visual Studio Build Tools is required.
    exit /b 1
)

set "VS_PATH="

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    set "VS_PATH=%%i"
)

if not defined VS_PATH (
    echo ERROR: Visual Studio C++ Build Tools not found.
    exit /b 1
)

REM ============================================================
REM Find CMake for the NeMo build
REM
REM Prefer PATH, then use the copy bundled with Visual Studio.
REM Keep this independent from the loqel application build cache.
REM ============================================================

set "CMAKE_EXE="

for /f "delims=" %%i in ('where cmake.exe 2^>nul') do (
    if not defined CMAKE_EXE set "CMAKE_EXE=%%i"
)

if not defined CMAKE_EXE (
    set "CMAKE_EXE=%VS_PATH%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
)

if not exist "%CMAKE_EXE%" (
    echo ERROR: CMake was not found in PATH or Visual Studio.
    echo.
    echo Install the CMake component with the Visual Studio Installer.
    exit /b 1
)

echo CMake:
echo %CMAKE_EXE%
echo.

REM ============================================================
REM Initialize MSVC
REM ============================================================

call "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat"

if errorlevel 1 (
    echo ERROR: Failed to initialize MSVC.
    exit /b 1
)

REM vcvars may change VCPKG_ROOT.
REM Force it to the vcpkg installation we detected above.
set "VCPKG_ROOT=%VCPKG_DIR%"

echo.
echo MSVC:
where cl
echo.

echo vcpkg:
echo %VCPKG_DIR%\vcpkg.exe
echo.

REM ============================================================
REM Check Ninja
REM ============================================================

set "NINJA_EXE="

for /f "delims=" %%i in ('where ninja.exe 2^>nul') do (
    if not defined NINJA_EXE set "NINJA_EXE=%%i"
)

if not defined NINJA_EXE (
    set "NINJA_EXE=%VS_PATH%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
)

if not exist "%NINJA_EXE%" (
    echo ERROR: Ninja was not found in PATH or Visual Studio.
    echo.
    echo Install the CMake component with the Visual Studio Installer.
    exit /b 1
)

echo Ninja:
echo %NINJA_EXE%
echo.

if /i "%~1"=="--check-tools" (
    echo NeMo build tools are available.
    exit /b 0
)

REM ============================================================
REM Install SentencePiece
REM
REM Run FROM the vcpkg directory so a vcpkg.json in the loqel
REM repository cannot accidentally trigger manifest mode.
REM ============================================================

echo.
echo ============================================================
echo  Installing SentencePiece
echo ============================================================
echo.

pushd "%VCPKG_DIR%"

vcpkg.exe install sentencepiece:%TRIPLET%

if errorlevel 1 (
    popd
    echo.
    echo ERROR: Failed to install SentencePiece.
    exit /b 1
)

popd

REM ============================================================
REM Verify SentencePiece
REM ============================================================

echo.
echo Checking SentencePiece...
echo.

if not exist "%VCPKG_DIR%\installed\%TRIPLET%\lib\sentencepiece.lib" (
    echo ERROR: sentencepiece.lib was not found at:
    echo %VCPKG_DIR%\installed\%TRIPLET%\lib\sentencepiece.lib
    echo.
    exit /b 1
)

echo Found:
echo %VCPKG_DIR%\installed\%TRIPLET%\lib\sentencepiece.lib

REM ============================================================
REM Clean old CMake configuration
REM ============================================================

echo.
echo ============================================================
echo  Cleaning old build
echo ============================================================
echo.

if exist "%BUILD_DIR%" (
    rmdir /s /q "%BUILD_DIR%"
)

REM ============================================================
REM Configure
REM ============================================================

echo.
echo ============================================================
echo  Configuring
echo ============================================================
echo.

"%CMAKE_EXE%" ^
  -S "%NEMO_DIR%" ^
  -B "%BUILD_DIR%" ^
  -G Ninja ^
  -DCMAKE_MAKE_PROGRAM="%NINJA_EXE%" ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_C_COMPILER=cl ^
  -DCMAKE_CXX_COMPILER=cl ^
  -DCMAKE_TOOLCHAIN_FILE="%VCPKG_DIR%\scripts\buildsystems\vcpkg.cmake" ^
  -DVCPKG_TARGET_TRIPLET=%TRIPLET% ^
  -DCMAKE_PREFIX_PATH="%VCPKG_DIR%\installed\%TRIPLET%" ^
  -DSENTENCEPIECE_LIB="%VCPKG_DIR%\installed\%TRIPLET%\lib\sentencepiece.lib" ^
  -DGGML_CCACHE=OFF ^
  -DNEMO_SPEECH_GGML_PATCHED=OFF ^
  -DGGML_CUDA=OFF ^
  -DGGML_VULKAN=OFF ^
  -DNEMO_SPEECH_BUILD_ASR=ON ^
  -DNEMO_SPEECH_BUILD_DIAR=OFF ^
  -DNEMO_SPEECH_BUILD_TTS=OFF ^
  -DNEMO_SPEECH_BUILD_NMT=OFF ^
  -DNEMO_SPEECH_BUILD_CLI=OFF ^
  -DNEMO_SPEECH_BUILD_HTTP=OFF ^
  -DNEMO_SPEECH_BUILD_GRPC=OFF ^
  -DNEMO_SPEECH_BUILD_EXAMPLES=OFF ^
  -DNEMO_SPEECH_BUILD_TESTS=OFF ^
  -DNEMO_SPEECH_BUILD_TOOLS=OFF ^
  -DNEMO_SPEECH_WITH_FLASHLIGHT=OFF ^
  -DNEMO_SPEECH_WITH_NORM=OFF

if errorlevel 1 (
    echo.
    echo ============================================================
    echo  ERROR: CMake configuration failed
    echo ============================================================
    exit /b 1
)

REM ============================================================
REM Build
REM ============================================================

echo.
echo ============================================================
echo  Building
echo ============================================================
echo.

"%CMAKE_EXE%" --build "%BUILD_DIR%" --parallel

if errorlevel 1 (
    echo.
    echo ============================================================
    echo  ERROR: Build failed
    echo ============================================================
    exit /b 1
)

REM ============================================================
REM Install
REM ============================================================

echo.
echo ============================================================
echo  Installing NeMo-Speech.cpp
echo ============================================================
echo.

"%CMAKE_EXE%" --install "%BUILD_DIR%" --prefix "%INSTALL_DIR%"

if errorlevel 1 (
    echo.
    echo ============================================================
    echo  ERROR: Install failed
    echo ============================================================
    exit /b 1
)

REM ============================================================
REM Done
REM ============================================================

echo.
echo ============================================================
echo  SUCCESS
echo ============================================================
echo.
echo Installed to:
echo %INSTALL_DIR%
echo.

endlocal
exit /b 0
