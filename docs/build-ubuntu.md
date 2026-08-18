# Building on Ubuntu

## Current status

The portable formatter, audio queue, ASR wrapper, and dictation controller can
be compiled on Ubuntu. The Linux desktop adapter is not implemented yet, so this
does not currently produce a runnable tray application.

X11 and Wayland must be treated as separate product targets. X11 can provide
behavior close to the Windows application. Wayland deliberately restricts
global observation and synthetic input, so automatic insertion cannot be
assumed to work across compositors.

## Supported starting point

- Ubuntu 24.04 x86-64;
- GCC 13 or newer;
- CMake 3.26 or newer;
- Ninja;
- a CPU ASR build for the first porting milestone.

Install the build tools:

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build git pkg-config
```

## Build and install NeMo Speech

Run these commands from the repository root:

```bash
git -C NeMo-Speech.cpp submodule update --init ggml
cd NeMo-Speech.cpp
./scripts/configure.sh cpu-asr
cmake --build --preset cpu-asr
cmake --install build/cpu-asr --prefix "$PWD/install-cpu-min"
cd ..
```

## Build the portable core and tests

```bash
cmake -S . -B build-ubuntu -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DLOQEL_BUILD_APP=OFF \
  -DLOQEL_BUILD_CORE=ON \
  -DNEMO_SDK_DIR="$PWD/NeMo-Speech.cpp/install-cpu-min"

cmake --build build-ubuntu
ctest --test-dir build-ubuntu --output-on-failure
```

`LOQEL_BUILD_APP=ON` intentionally fails until the Linux adapter exists.

## Desktop adapter work required

The Linux implementation should use `ApplicationCore` and provide:

1. An application host implementing `IApplicationPlatform`.
2. PipeWire capture, with PulseAudio compatibility only if required, behind
   `IAudioCapture`.
3. A StatusNotifierItem tray implementation plus a normal-window fallback for
   desktops without a usable tray.
4. Settings and overlay windows through one selected GUI toolkit.
5. XDG configuration paths and a per-user D-Bus single-instance name.
6. UI-loop dispatch for `post_to_main`.

For X11, add a global trigger, active-window validation, and XTest-based text
insertion. Test keyboard-layout and Unicode behavior rather than assuming the
Windows `SendInput` behavior transfers directly.

For Wayland, use the XDG Global Shortcuts portal for the trigger. Choose an
explicit insertion policy before implementation:

- clipboard-only mode with a notification asking the user to paste;
- a user-approved Remote Desktop/libei session where supported;
- or documented automatic-insertion support only in an X11 session.

Do not silently discard recognized text when insertion is unavailable.

## CUDA

Keep the initial Ubuntu port CPU-only. Add CUDA as a separate build and runtime
variant after the CPU application passes the same capture, ASR, and insertion
tests. The current `AsrEngine` explicitly requests the CPU backend.
