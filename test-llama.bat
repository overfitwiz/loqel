@echo off
setlocal EnableExtensions

REM ============================================================
REM Paths
REM ============================================================

cd /d "%~dp0"

set "ROOT=%~dp0"
set "TEST_DIR=%ROOT%tests\llama"
set "BUILD_DIR=%ROOT%build\llama-test"
set "LLAMA_INSTALL_DIR=%ROOT%install\llama-cpu-min"

echo.
echo ============================================================
echo  llama.cpp Smoke Test Build
echo ============================================================
echo.

REM ============================================================
REM Check sources
REM ============================================================

if not exist "%TEST_DIR%\CMakeLists.txt" (
    echo ERROR: llama test CMakeLists.txt not found:
    echo %TEST_DIR%\CMakeLists.txt
    exit /b 1
)

if not exist "%TEST_DIR%\test-llama.cpp" (
    echo ERROR: llama test source not found:
    echo %TEST_DIR%\test-llama.cpp
    exit /b 1
)

REM ============================================================
REM Check llama install
REM ============================================================

if not exist "%LLAMA_INSTALL_DIR%" (
    echo ERROR: llama.cpp install directory not found:
    echo %LLAMA_INSTALL_DIR%
    echo.
    echo Build llama.cpp first:
    echo.
    echo   build-llama.bat
    echo.
    exit /b 1
)

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
REM Find CMake
REM
REM Prefer PATH, then use the copy bundled with Visual Studio.
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

echo.
echo MSVC:
where cl
echo.

REM ============================================================
REM Find Ninja
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
    echo llama.cpp test build tools are available.
    exit /b 0
)

REM ============================================================
REM Clean old test build
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
echo  Configuring llama.cpp test
echo ============================================================
echo.

"%CMAKE_EXE%" ^
  -S "%TEST_DIR%" ^
  -B "%BUILD_DIR%" ^
  -G Ninja ^
  -DCMAKE_MAKE_PROGRAM="%NINJA_EXE%" ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_C_COMPILER=cl ^
  -DCMAKE_CXX_COMPILER=cl ^
  -DCMAKE_PREFIX_PATH="%LLAMA_INSTALL_DIR%"

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
echo  Building llama.cpp test
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
REM Verify executable
REM ============================================================

set "TEST_EXE=%BUILD_DIR%\test-llama.exe"

if not exist "%TEST_EXE%" (
    echo.
    echo ERROR: Test executable was not created:
    echo %TEST_EXE%
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
echo Test executable:
echo %TEST_EXE%
echo.
echo Run it with:
echo.
echo   "%TEST_EXE%" models\your-model.gguf
echo.

endlocal
exit /b 0

