# NeMo Talk

NeMo Talk is a local push-to-talk dictation application. The complete desktop
adapter currently targets Windows: hold F8 to
capture microphone audio, watch the live transcript in a non-activating overlay,
and release F8 to finalize recognition and insert the text into the application
that was active when dictation began.

The application runs locally and uses the NeMo Speech C API with a GGUF
streaming ASR model. The dictation controller, queue, recognizer wrapper, and
Markdown formatter are platform-neutral; Win32 and WASAPI provide the current
desktop and microphone adapters. There is no web or service layer.

## Build status

- **Windows:** complete tray application.
- **macOS:** portable core builds; native desktop adapter remains to be added.
- **Ubuntu:** portable core builds; X11/Wayland desktop adapters remain to be
  added.

## Documentation

- [Using the application](docs/application.md) describes startup, dictation,
  model discovery, visible behavior, and current limitations.
- [Windows build guide](docs/build-windows.md) builds the complete tray
  application and tests.
- [Source map](docs/source-map.md) explains what belongs in every app-owned
  source file and the important top-level directories.
- [Architecture and data flows](docs/architecture.md) documents components,
  threads, message ownership, state transitions, shutdown, and audio/text data
  flow.
- [macOS build and port guide](docs/build-macos.md) covers the native NeMo SDK,
  portable-core build, permissions, and remaining adapter work.
- [Ubuntu build and port guide](docs/build-ubuntu.md) covers the native NeMo
  SDK, portable-core build, and the X11/Wayland capability split.
- [Implementation plan](docs/plan) records the original incremental development
  sequence.

## Scope

The app-owned implementation is in `src/`; portable interfaces and orchestration
are in `src/core/`, and Windows boundary helpers are in `src/windows/`.
`NeMo-Speech.cpp/` is the upstream speech SDK source tree. NeMo Talk consumes
its installed public C API and CMake target rather than SDK internals.
