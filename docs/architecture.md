# Architecture

## Goal

NeMo Talk is split into a platform-neutral dictation core and a native desktop
adapter. Windows is currently the only complete adapter. macOS and Ubuntu can
build the portable code once a native NeMo Speech SDK is installed.

## Dependency direction

```text
Windows entry point and UI
        |
        v
IApplicationPlatform <---- ApplicationCore ----> IAudioCapture
                              |     |     |
                              |     |     +-- MarkdownFormatter
                              |     +-------- AudioQueue
                              +-------------- AsrEngine / NeMo C API
```

`ApplicationCore` owns session state, the ASR stream, the per-session queue,
the ASR consumer thread, settings snapshots, transcript aggregation, formatting,
focus-safety decisions, and ordered shutdown. It contains no OS handles or UI
headers.

`IApplicationPlatform` provides the operations that differ across desktop
systems:

- capture and validate the application that should receive the transcript;
- show and hide the overlay;
- display errors and warnings;
- insert UTF-8 text;
- deliver callbacks to the main UI thread.

`IAudioCapture` supplies mono float samples and the capture sample rate through
`AudioQueue`. Its producer must close or fail the queue before returning.

## Text encoding

All portable code uses UTF-8 `std::string`. The NeMo C API already emits UTF-8.
The Windows adapter converts UTF-8 to UTF-16 only at Win32 boundaries through
`WindowsText`.

Markdown command matching is ASCII case-insensitive. Non-ASCII command text is
matched byte-for-byte and all non-command UTF-8 text is preserved.

## Windows adapter

`App` implements `IApplicationPlatform` using:

- a hidden Win32 window as the main-thread dispatcher;
- `TrayIcon`, `Overlay`, and `SettingsWindow` for UI;
- `Hotkey` for the global F8 down/up trigger;
- `WindowsAudioCapture` for WASAPI capture;
- `TextInjector` for `SendInput` insertion;
- `FormattingSettings` for the Local AppData INI file.

Worker callbacks are heap-owned `std::function<void()>` tasks posted to the
hidden window. The UI thread owns and destroys each accepted task. Shutdown
joins both audio threads and removes unhandled tasks before destroying UI
resources.

## Session lifecycle

```text
Idle
  -- trigger down --> Listening
  -- trigger up ----> Finalizing
  -- ASR done ------> validate target -> insert text -> Idle
```

At trigger-down, the core snapshots Markdown, custom-dictionary, recognition
mode, and latency settings and captures an opaque target token. Edits made
during dictation affect only the next session. At completion, text is inserted
only if the same target is still active.

The audio producer and ASR consumer remain separate. The queue permits at most
three seconds of buffered audio; exceeding that limit fails the session instead
of allowing unbounded memory use and latency.

In live mode, the consumer pushes every chunk into a NeMo streaming request and
publishes partial results. In record-first mode, it continuously drains chunks
into a recording buffer, then calls `nemo_speech_asr_recognize_f32` after capture
closes. Both paths send their final UTF-8 transcript through the same
`MarkdownFormatter` and insertion flow.

The RNNT latency preset maps to right-context values `1`, `6`, or `-1` (model
default). Because this setting belongs to recognizer construction, changing it
reloads the recognizer. A failed reload attempts to restore the previous preset.

## Adding another platform

A new platform must provide:

1. an `IApplicationPlatform` implementation;
2. an `IAudioCapture` implementation;
3. an entry point/event loop that maps trigger and settings events to the core;
4. settings persistence, model discovery, and single-instance behavior;
5. packaging and permission metadata;
6. native integration tests for capture, focus validation, and insertion.

Platform code must not move into `ApplicationCore`. When an OS cannot provide a
capability, such as unrestricted Wayland insertion, the adapter should expose a
clear fallback rather than weakening the core's safety guarantees.
