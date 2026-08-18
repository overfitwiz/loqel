# Building on Windows

## Requirements

- Visual Studio with the Desktop development with C++ workload;
- CMake 3.26 or newer;
- Ninja;
- an installed NeMo Speech ASR SDK for Windows.

The repository currently defaults `NEMO_SDK_DIR` to
`NeMo-Speech.cpp/install-cpu-min`. To create that SDK with the bundled source,
follow `NeMo-Speech.cpp/docs/development/windows-build.md` and select its
CPU/ASR-only build, then install it into that prefix.

## Configure and build

Run from an x64 Visual Studio Developer PowerShell:

```powershell
cmake -S . -B build-windows -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DNEMO_SDK_DIR="$PWD\NeMo-Speech.cpp\install-cpu-min" `
  -DBUILD_TESTING=ON

cmake --build build-windows
ctest --test-dir build-windows --output-on-failure
```

The application is written to `build-windows/loqel.exe`. CMake copies the
NeMo runtime DLLs from the SDK's `bin` directory beside the executable.

## Run

Place the model under `models/`, set `NEMO_SPEECH_MODEL`, or pass it explicitly:

```powershell
build-windows\loqel.exe --model C:\path\to\model.gguf
```
