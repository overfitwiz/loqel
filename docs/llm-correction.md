# LLM text correction

## Purpose and position in the pipeline

loqel uses the local `LFM2.5-350M-Q8_0.gguf` model as an optional final text
postprocessor. It normalizes dictated forms such as `github dot com`, spoken
numbers, dates, currency, units, email addresses, and punctuation while asking
the model to preserve wording and meaning.

Correction is deliberately the last text transformation:

```text
NeMo final transcript
  -> remove configured filler words (formatted mode only)
  -> expand Markdown voice commands (formatted mode only)
  -> LLM correction (when enabled)
  -> target-window validation
  -> text insertion
```

Partial streaming results shown in the overlay do not go through the LLM. The
model runs once, after NeMo has finalized the complete dictation. Both formatted
and plain dictation use correction when the global option is enabled.

## Model lifetime

LLM correction is enabled by default. At application startup, the app loads the
model on the CPU and keeps the model and inference context resident in RAM. The
**Correct final text with the local LLM** checkbox is on the General settings
page and is saved as `llm_correction.enabled` in `settings.ini`.

Clearing the checkbox releases the sampler, context, and model immediately.
Selecting it again reloads them. A mutex serializes inference and setting
changes, so the model cannot be released while a correction is running. If the
app starts with correction disabled, it does not load the LLM.

## Prompt and KV-cache reuse

The implementation in `src/LlmPostprocessor.cpp` follows
`tests/llama/test-llama.cpp`:

1. Tokenize and evaluate the fixed normalization instructions and examples once
   when the model loads.
2. Format the compact system instruction and its preservation example with the
   chat template embedded in the GGUF model. Snapshot sequence 0 after that
   fixed prompt, including its attention KV cache
   and recurrent state.
3. Before each dictation, clear the previous dynamic state and restore that
   fixed-prompt checkpoint. This is required for hybrid recurrent models such as
   LFM2.5, which cannot reliably remove an arbitrary sequence suffix.
4. Append the current dictation as a user message and the model's assistant
   generation marker at the first dynamic position.
5. Generate reproducibly with the model-recommended low-temperature top-k
   sampling and a fixed seed until an end token. The output allowance is sized
   from the input and remaining context rather than a fixed token limit, so a
   token-limit exit is detectable instead of being accepted as complete text.

Consequently, repeated dictations restore rather than reevaluate the long
correction prompt; only the current dictation and generated answer are evaluated
each time.

The context size is 2048 tokens and the batch size is 1024. If the context cannot
hold an approximately full-length response, correction is bypassed. Empty,
token-truncated, line-losing, or materially shorter/longer generated text is
also rejected, and the original recognized text is retained. Llama API errors
still abort insertion and display an error.

## LLM model discovery

The first available source wins:

1. `--llm-model <path>`
2. `LOQEL_LLM_MODEL`
3. `models\LFM2.5-350M-Q8_0.gguf`, searching upward from the executable
4. the same search upward from the current working directory

Run `download-llm.bat` to place the default model in `models\`. A missing model
is allowed only when LLM correction is disabled in the saved settings.

## Build integration

`build-llama.bat` installs the static llama SDK to
`install\llama-cpu-min`. `build-app.bat` verifies that installation and passes it
as `LLAMA_SDK_DIR`. The root CMake project finds the installed `llama` package,
links it into `loqel_core`, and includes `src/LlmPostprocessor.cpp`. No source in
`NeMo-Speech.cpp` or its nested submodules is modified.
