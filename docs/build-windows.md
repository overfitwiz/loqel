# Building on Windows

## Requirements

- Visual Studio with the Desktop development with C++ workload;
- CMake 3.26 or newer;
- Ninja;
- an installed NeMo Speech ASR SDK for Windows;
- an installed llama.cpp SDK for Windows.

The repository currently defaults `NEMO_SDK_DIR` to
`install/nemo-cpu-min`. Run `build-nemo.bat` to build the bundled source and
install the CPU/ASR SDK into that root-level prefix.

Run `build-llama.bat` to build the bundled llama.cpp source without changing it
and install the static SDK to `install/llama-cpu-min`.

## Configure and build

Run from an x64 Visual Studio Developer PowerShell:

```powershell
cmake -S . -B build-windows -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DNEMO_SDK_DIR="$PWD\install\nemo-cpu-min" `
  -DLLAMA_SDK_DIR="$PWD\install\llama-cpu-min" `
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

Run `download-llm.bat` for the default correction model. Its path may instead
be supplied with `--llm-model` or `LOQEL_LLM_MODEL`. The app can start without
that model only when correction is disabled in saved settings.
