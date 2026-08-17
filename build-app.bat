@echo off
setlocal EnableExtensions

REM Always switch to the directory containing this BAT.
cd /d "%~dp0"

set "BUILD_DIR=build"
set "NEMO_SDK=NeMo-Speech.cpp\install-cpu-min"
set "TRIPLET=x64-windows-static-md"

REM ============================================================
REM Find vcpkg
REM ============================================================

REM Use VCPKG_ROOT if the user has configured it.
REM Otherwise fall back to C:\vcpkg.

set "VCPKG_DIR=%VCPKG_ROOT%"

if not defined VCPKG_DIR set "VCPKG_DIR=C:\vcpkg"

if not exist "%VCPKG_DIR%\vcpkg.exe" (
    echo ERROR: vcpkg not found.
    echo.
    echo Set VCPKG_ROOT to your vcpkg installation
    echo or install vcpkg at C:\vcpkg
    exit /b 1
)

if not exist "%VCPKG_DIR%\scripts\buildsystems\vcpkg.cmake" (
    echo ERROR: vcpkg CMake toolchain not found.
    exit /b 1
)

REM ============================================================
REM Find Visual Studio
REM ============================================================

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

REM ============================================================
REM Initialize MSVC
REM ============================================================

call "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat"

if errorlevel 1 (
    echo ERROR: Failed to initialize MSVC.
    exit /b 1
)

REM vcvars may modify VCPKG_ROOT.
REM Restore the vcpkg installation selected above.
set "VCPKG_ROOT=%VCPKG_DIR%"

REM ============================================================
REM Check dependencies
REM ============================================================

if not exist "%NEMO_SDK%" (
    echo ERROR: NeMo SDK not found:
    echo %CD%\%NEMO_SDK%
    echo.
    echo Run build-nemo.bat first.
    exit /b 1
)

where cmake >nul 2>&1

if errorlevel 1 (
    echo ERROR: CMake not found.
    exit /b 1
)

where ninja >nul 2>&1

if errorlevel 1 (
    echo ERROR: Ninja not found.
    exit /b 1
)

REM ============================================================
REM Configure
REM ============================================================

echo.
echo ========================================
echo Configuring Loqel
echo ========================================
echo.

cmake ^
    -S . ^
    -B "%BUILD_DIR%" ^
    -G Ninja ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_C_COMPILER=cl ^
    -DCMAKE_CXX_COMPILER=cl ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG_DIR%\scripts\buildsystems\vcpkg.cmake" ^
    -DVCPKG_TARGET_TRIPLET=%TRIPLET% ^
    -DNEMO_SDK_DIR="%NEMO_SDK%" ^
    -DNEMO_TALK_BUILD_APP=ON ^
    -DNEMO_TALK_BUILD_CORE=ON ^
    -DBUILD_TESTING=OFF

if errorlevel 1 (
    echo.
    echo ========================================
    echo ERROR: App configuration failed.
    echo ========================================
    exit /b 1
)

REM ============================================================
REM Build
REM ============================================================

echo.
echo ========================================
echo Building Loqel
echo ========================================
echo.

cmake --build "%BUILD_DIR%" --parallel

if errorlevel 1 (
    echo.
    echo ========================================
    echo ERROR: App build failed.
    echo ========================================
    exit /b 1
)

REM ============================================================
REM Success
REM ============================================================

echo.
echo ========================================
echo SUCCESS
echo ========================================
echo.
echo Executable:
echo %CD%\%BUILD_DIR%\speech_app.exe
echo.

endlocal
exit /b 0