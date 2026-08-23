# Manual verification

## Custom dictionary

1. Start the tray app and open **Settings...**.
2. Add a distinctive word or phrase, one entry per line, and leave the boost at
   `2` for the first test.
3. Save, begin a new F8 dictation session, and speak the entry in a short
   sentence several times.
4. Confirm that recognition completes normally and that the text is pasted into
   the window that was focused when F8 was pressed.
5. Reopen Settings and confirm the entries and boost persisted.
6. Set boost to `0`, save, and confirm a new session still recognizes normally
   with dictionary biasing disabled.

## Validation

- Duplicate dictionary entries are rejected case-insensitively.
- Dictionary boosts outside `0` to `5`, and non-numeric values, are rejected.

## Streaming latency

1. Select **Live transcription** and **Low** latency, save, and start a new
   session. Confirm partial text appears while F8 is held.
2. Repeat with **Balanced** and **Highest accuracy**. Confirm all three sessions
   complete and that higher-context modes take longer to produce partial text.
3. Save a different latency while a session is active. Confirm the current
   session completes normally and the next session uses the new preset.
4. Restart the app and confirm the selected preset persisted.

## Record first, then transcribe

1. Select **Record first, then transcribe** and save.
2. Hold F8 and dictate `heading release notes paragraph bold ready bold`.
3. Confirm the overlay says `Recording...` and does not show partial text.
4. Release F8 and confirm it changes to `Transcribing...`.
5. Confirm the inserted result is `# release notes`, a blank line, then
   `**ready**`, followed by the normal trailing space.
6. Change focus before transcription finishes and confirm no text is inserted.
7. Restart the app and confirm record-first mode persisted.
