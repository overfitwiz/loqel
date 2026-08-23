# Source Map

## Repository layout

| Path | Responsibility |
| --- | --- |
| `CMakeLists.txt` | Builds the portable targets and, on Windows, the complete native application. |
| `src/core/` | OS-independent application interfaces, settings types, and dictation controller. |
| `src/windows/` | Win32 UTF-8/UTF-16 boundary helpers. |
| `src/` | Shared ASR/formatting/queue code plus the current Windows adapter components. |
| `tests/` | Platform-neutral unit tests and manual test material. |
| `models/` | Local GGUF model location; model files are ignored by Git. |
| `NeMo-Speech.cpp/` | Upstream NeMo Speech SDK source and per-platform install prefixes. |
| `docs/` | Architecture, usage, build/port guides, and project notes. |

Generated `build-*` directories are not part of the source architecture.

## Portable core

### `src/core/ApplicationCore.*`

Owns the dictation state machine, settings snapshots, live/offline recognition
selection, `AudioQueue`, ASR consumer thread, `AsrEngine`, transcript
aggregation, cleanup, focus-safety decision, insertion request, and
deterministic session shutdown.

It communicates with the desktop only through `IApplicationPlatform` and with
the microphone only through `IAudioCapture`.

### `src/core/ApplicationPlatform.h`

Defines the opaque active-target token and all UI/platform operations needed by
the core: target capture and validation, overlay, messages, insertion, and
main-thread callback delivery.

### `src/core/AudioCapture.h`

Defines the capture interface. An implementation pushes mono float chunks into
`AudioQueue`, publishes the sample rate once, and closes or fails the queue when
capture ends.

### `src/core/ApplicationSettings.h`

Contains UTF-8 custom-dictionary data, recognition mode, latency presets, and
the preset-to-RNNT-context mapping shared by the core and platform settings UI.

### Shared implementation files

- `AudioQueue.*` is the bounded producer/consumer channel.
- `AsrEngine.*` owns NeMo recognizer and stream handles through the public C API.
- `LlmPostprocessor.*` owns the resident llama model, cached correction prompt,
  dynamic KV suffix, and deterministic final-text generation.

## Windows application adapter

### `src/main.cpp`

Owns the Win32 entry point, named single-instance mutex, Windows command-line
parsing, model discovery, and `App` lifetime.

### `src/App.*`

Implements `IApplicationPlatform`. It creates the hidden dispatcher window,
owns Windows UI components, converts worker callbacks into UI-thread tasks,
loads and saves settings, and maps hotkey/menu messages to `ApplicationCore`.
It does not implement dictation or ASR state transitions.

### Native components

- `AudioCapture.*`: `WindowsAudioCapture`, a WASAPI `IAudioCapture` adapter.
- `Hotkey.*`: global low-level F8 key-down/up hook.
- `TrayIcon.*`: notification-area icon and menu.
- `Overlay.*`: non-activating topmost transcript window.
- `SettingsWindow.*`: native formatting/dictionary editor.
- `TextInjector.*`: UTF-16 `SendInput` adapter.
- `FormattingSettings.*`: Local AppData INI persistence.
- `Paths.*`: Windows executable, ASR-model, and LLM-model path discovery.
- `windows/WindowsText.*`: UTF-8/UTF-16 conversion at Win32 boundaries.

## Tests

Future platform adapters should add native integration tests for audio capture,
trigger delivery, focus changes, text
insertion, permission denial, and packaging.
