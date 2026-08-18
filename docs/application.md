# Using loqel

## Purpose

loqel turns a held F8 keypress into text in the current Windows application.
Recognition is performed locally with the configured NeMo Speech model.

The application has no normal taskbar window. Its persistent UI is a system tray
icon, and its temporary UI is an overlay near the bottom of the working area.

## Startup

Only one instance may run in a Windows login session. On startup, the program:

1. Acquires the `Local\\loqel.PushToTalk` mutex.
2. Resolves the ASR model path.
3. Creates a hidden application window, tray icon, and overlay.
4. Loads the ASR model on the main thread.
5. Installs a global low-level keyboard hook for F8.
6. Enters the Win32 message loop.

If another instance owns the mutex, the new process displays a message and
exits. Startup also stops with a visible error if the model, tray icon, overlay,
recognizer, or keyboard hook cannot be initialized.

## Model discovery

The model is selected once during startup. The first matching source wins:

1. `--model <path>` on the command line.
2. The first positional argument that does not start with `-`.
3. The `NEMO_SPEECH_MODEL` environment variable.
4. `models\\nemotron-speech-streaming-en-0.6b.q8_0.gguf`, then the retained
   `models\\nemotron-speech-streaming-en-0.6b.new.q8_0.gguf` name, searching six
   directory levels starting at the executable directory.
5. The same upward search from the current working directory, in that order.

Command-line, positional, and environment paths are converted to absolute paths.
The path must identify a regular file.

## Dictation workflow

1. Focus the target application and place its caret where text should appear.
2. Press and hold F8.
3. The overlay displays `Listening...` and then live recognition results.
4. Speak while continuing to hold F8.
5. Release F8. The overlay displays `Finalizing...` while remaining audio and ASR
   results are processed.
6. If the original target window is still the foreground window, the final text
   is inserted followed by one space.

The recognition mode is selected in **Settings...**:

- **Live transcription** sends audio to NeMo while F8 is held and displays
  partial transcripts.
- **Record first, then transcribe** records while F8 is held, displays no live
  transcript, and runs one whole-utterance transcription after F8 is released.

Both modes apply the same custom dictionary, automatic punctuation, Markdown
commands, target-window validation, and final text insertion rules.

F8 is consumed by the global hook, so the foreground application does not receive
that keystroke. Repeated key-down events generated while F8 is held do not start
additional sessions.

Only one recording/finalization session can exist at a time. Pressing F8 again
while a session is active or finalizing is ignored.

## Overlay behavior

The overlay is:

- topmost;
- absent from the taskbar and Alt+Tab list;
- shown without activation, so it should not steal keyboard focus;
- mouse-transparent;
- positioned above the bottom edge of the current Windows work area.

Partial transcripts replace the displayed text as NeMo emits updates. Spoken
Markdown commands are applied to each update, so the overlay shows the literal
Markdown that will be inserted, including line breaks, list markers, heading
markers, and emphasis markers. The overlay is hidden when a session finishes.

## Markdown commands

The final transcript is passed through a deterministic Markdown formatter before
insertion. Command matching is case-insensitive. The default phrases are:

| Spoken phrase | Result |
| --- | --- |
| `list` | Inserts a new unordered-list `- ` item. Each use inserts another item. |
| `item` | Inserts a new unordered-list `- ` item. |
| `bold` | Inserts one `**` marker. Use it again to insert the closing `**`. |
| `italic` | Inserts one `*` marker. Use it again to insert the closing `*`. |
| `heading` | Starts a level-one `#` heading. |
| `subheading` | Starts a level-two `##` heading. |
| `paragraph` | Inserts a blank line. |

Triggers never add a matching closing marker automatically. Punctuation that
ASR adds immediately after an opening or structural trigger is treated as
trigger punctuation and removed.

## Formatting settings

Open the tray menu and select `Settings...` to customize every trigger. A
trigger may be a single word or a phrase, and must be non-empty and unique,
ignoring case.

The same window contains a custom dictionary for words and phrases that the
recognizer commonly misses. Enter one item per line and set its speech-context
boost from 0 to 5. The default boost is 2; values from 2 to 3 are generally a
useful starting point. A value of 0 disables dictionary biasing without deleting
the entries. Excessive boosting can make the recognizer insert a phrase when it
was not spoken.

Saving applies both command and dictionary changes to the next dictation
session. An already-running session continues with the settings snapshot it
started with.

The settings window also controls streaming latency:

| Preset | RNNT right context | Tradeoff |
| --- | ---: | --- |
| Low | `1` | Approximately 160 ms; fastest partial results, less context. |
| Balanced | `6` | Approximately 560 ms; the default and previous app behavior. |
| Highest accuracy | model default (`-1`) | Uses the model's trained context and has the greatest delay. |

Changing latency reloads the recognizer. If settings are saved during an active
session, its existing snapshot remains in use and the latency is applied before
the next session. Latency affects live streaming; offline recognition processes
the completed recording as a whole.

Settings are persisted to:

```text
%LOCALAPPDATA%\loqel\settings.ini
```

## Text insertion safety

At the beginning of a session, the app remembers the foreground window. Before
inserting the final transcript, it verifies that the window still exists and is
still in the foreground. If focus changed during dictation, no text is inserted.

Text is injected as UTF-16 keyboard input through `SendInput`. Windows integrity
rules prevent a normal process from injecting input into an elevated process. In
that case, the application displays an error.

Final text is normalized before insertion:

- carriage returns, newlines, and tabs become spaces;
- leading and trailing spaces are removed;
- consecutive spaces collapse to one space;
- one trailing space is appended for continued typing.

## Tray icon and exit

Left- or right-click the tray icon to open its menu. The menu contains
`Settings...` and `Exit`. Exiting removes the keyboard hook, stops capture, waits
for worker threads, closes the ASR stream, destroys UI resources, and removes the
tray icon.

## Error behavior

Startup and session errors are displayed with native Windows message boxes. Audio
capture failures travel through the audio queue to the ASR consumer thread and
then back to the UI thread. ASR API errors include the last error reported by the
NeMo Speech library.

The audio queue is bounded to approximately three seconds. If recognition falls
farther behind capture, the queue closes the session with an error instead of
allowing memory use and latency to grow indefinitely.

## Current limitations

- The trigger is fixed to F8 and is not configurable.
- The app uses the Windows default communications capture device, falling back to
  the default console capture device.
- The model is loaded synchronously during startup.
- Applying a new streaming-latency preset reloads the model synchronously.
- Record-first mode keeps the current recording in memory until transcription
  finishes.
- There is no model picker, device picker, or tray status item.
- Text insertion depends on `SendInput` and the Windows foreground/integrity
  restrictions described above.
- Very long overlay transcripts may be clipped by the available work area.
- The app does not currently restore its tray icon after Windows Explorer is
  restarted.
