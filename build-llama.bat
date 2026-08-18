@echo off
setlocal EnableExtensions

REM ============================================================
REM Paths
REM ============================================================

cd /d "%~dp0"

set "ROOT=%~dp0"
set "LLAMA_DIR=%ROOT%NeMo-Speech.cpp\llama.cpp"
set "BUILD_DIR=%ROOT%build\llama-cpu-min"
set "INSTALL_DIR=%ROOT%install\llama-cpu-min"

echo.
echo ============================================================
echo  llama.cpp CPU Build
echo ============================================================
echo.

REM ============================================================
REM Check source
REM ============================================================

if not exist "%LLAMA_DIR%\CMakeLists.txt" (
    echo ERROR: llama.cpp not found:
    echo %LLAMA_DIR%
    echo.
    echo Make sure the NeMo-Speech.cpp submodules are initialized:
    echo.
    echo   git submodule update --init --recursive
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
    echo llama.cpp build tools are available.
    exit /b 0
)

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
  -S "%LLAMA_DIR%" ^
  -B "%BUILD_DIR%" ^
  -G Ninja ^
  -DCMAKE_MAKE_PROGRAM="%NINJA_EXE%" ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_C_COMPILER=cl ^
  -DCMAKE_CXX_COMPILER=cl ^
  -DBUILD_SHARED_LIBS=OFF ^
  -DGGML_CCACHE=OFF ^
  -DGGML_CUDA=OFF ^
  -DGGML_VULKAN=OFF ^
  -DGGML_OPENMP=OFF ^
  -DGGML_NATIVE=OFF ^
  -DLLAMA_BUILD_COMMON=OFF ^
  -DLLAMA_BUILD_TESTS=OFF ^
  -DLLAMA_BUILD_EXAMPLES=OFF ^
  -DLLAMA_BUILD_TOOLS=OFF ^
  -DLLAMA_BUILD_SERVER=OFF ^
  -DLLAMA_BUILD_APP=OFF ^
  -DLLAMA_BUILD_MTMD=OFF ^
  -DLLAMA_BUILD_UI=OFF ^
  -DLLAMA_OPENSSL=OFF ^
  -DLLAMA_SUBPROCESS=OFF ^
  -DLLAMA_TOOLS_INSTALL=OFF ^
  -DLLAMA_TESTS_INSTALL=OFF

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
echo  Installing llama.cpp
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