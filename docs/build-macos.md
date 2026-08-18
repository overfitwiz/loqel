# Building on macOS

## Current status

The portable formatter, audio queue, ASR wrapper, and dictation controller can
be compiled on macOS. The macOS desktop adapter is not implemented yet, so this
does not currently produce a runnable menu-bar application.

The implemented Windows adapter is the behavioral reference. See
`docs/architecture.md` for the interfaces a macOS adapter must provide.

## Supported starting point

- Apple Silicon Mac;
- macOS 13 or newer;
- Xcode Command Line Tools;
- CMake 3.26 or newer;
- Ninja;
- a CPU ASR build for the first porting milestone.

Install the build tools:

```bash
xcode-select --install
brew install cmake ninja
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

The application consumes the installed C API and CMake package; it does not
include NeMo implementation headers directly.

## Build the portable core and tests

```bash
cmake -S . -B build-macos -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DLOQEL_BUILD_APP=OFF \
  -DLOQEL_BUILD_CORE=ON \
  -DNEMO_SDK_DIR="$PWD/NeMo-Speech.cpp/install-cpu-min"

cmake --build build-macos
ctest --test-dir build-macos --output-on-failure
```

`LOQEL_BUILD_APP=ON` intentionally fails until the macOS adapter exists.

## Desktop adapter work required

A macOS implementation should use the portable `ApplicationCore` and provide:

1. An Objective-C++ application host implementing `IApplicationPlatform`.
2. A Core Audio capture class implementing `IAudioCapture` and producing mono
   float samples with the device sample rate.
3. An `NSStatusItem` menu, settings window, and non-activating overlay.
4. A Quartz event tap for push-to-talk key-down and key-up events.
5. Frontmost-process capture and validation before insertion.
6. Quartz event-based Unicode insertion with explicit failure reporting.
7. Main-run-loop dispatch for `post_to_main`.
8. Application Support storage and a per-user single-instance mechanism.

The `.app` bundle must include `NSMicrophoneUsageDescription`. Global keyboard
observation and synthetic input also require macOS privacy permission handling;
the UI should explain denied permissions and link the user to System Settings.

Start with a Developer ID distributed, notarized Apple Silicon build. Add a
universal binary only after the arm64 application is complete and tested.

## Metal

Keep the first port CPU-only. Although NeMo Speech/ggml can be built with Metal,
`AsrEngine` currently selects CPU explicitly. Metal should be introduced later
as a separately tested backend setting, not as part of the desktop port.
