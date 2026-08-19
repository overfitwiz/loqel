#include "llama.h"
#include "ggml-backend.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// ============================================================
// Configuration
// ============================================================

static const char * MODEL_PATH = "models\\LFM2.5-350M-Q8_0.gguf";

static constexpr int MAX_OUTPUT_TOKENS = 128;

// This whole prompt is evaluated ONCE and remains in the KV cache.
static const char * NORMALIZATION_PROMPT =
    "You are a speech-to-text normalization engine.\n"
    "\n"
    "Convert spoken or awkwardly transcribed text into the written form "
    "the speaker intended.\n"
    "\n"
    "You may normalize:\n"
    "- URLs and domain names\n"
    "- Email addresses\n"
    "- Numbers\n"
    "- Dates and times\n"
    "- Currency amounts\n"
    "- Percentages\n"
    "- Phone numbers\n"
    "- Units and measurements\n"
    "- Common symbols and punctuation\n"
    "- Spoken formatting such as dot, slash, at, dash, underscore and colon\n"
    "\n"
    "Rules:\n"
    "1. Preserve the original meaning.\n"
    "2. Preserve the original wording as much as possible.\n"
    "3. Do not summarize or paraphrase.\n"
    "4. Do not add information.\n"
    "5. Only normalize when the intended form is clear.\n"
    "6. If uncertain, leave the text unchanged.\n"
    "7. Return only the normalized text.\n"
    "8. Do not explain your changes.\n"
    "\n"
    "Examples:\n"
    "\n"
    "Input: go to github dot com\n"
    "Output: go to github.com\n"
    "\n"
    "Input: email me at john dot smith at gmail dot com\n"
    "Output: email me at john.smith@gmail.com\n"
    "\n"
    "Input: it costs twenty five dollars\n"
    "Output: it costs $25\n"
    "\n"
    "Input: that's fifty percent\n"
    "Output: that's 50%\n"
    "\n"
    "Input: the temperature is twenty two degrees celsius\n"
    "Output: the temperature is 22 degrees Celsius\n"
    "\n"
    "Input: I bought twenty apples\n"
    "Output: I bought 20 apples\n"
    "\n";

// ============================================================
// Tokenize
// ============================================================

static std::vector<llama_token> tokenize(
    const llama_vocab * vocab,
    const std::string & text,
    bool add_special
) {
    int n = llama_tokenize(
        vocab,
        text.c_str(),
        text.size(),
        nullptr,
        0,
        add_special,
        false
    );

    if (n == 0) {
        return {};
    }

    if (n > 0) {
        fprintf(stderr, "ERROR: unexpected tokenizer result\n");
        return {};
    }

    std::vector<llama_token> tokens(-n);

    n = llama_tokenize(
        vocab,
        text.c_str(),
        text.size(),
        tokens.data(),
        static_cast<int32_t>(tokens.size()),
        add_special,
        false
    );

    if (n < 0) {
        fprintf(stderr, "ERROR: tokenization failed\n");
        return {};
    }

    tokens.resize(n);

    return tokens;
}

// ============================================================
// Decode tokens at explicit KV positions
// ============================================================

static bool decode_tokens(
    llama_context * ctx,
    const std::vector<llama_token> & tokens,
    llama_pos start_pos
) {
    if (tokens.empty()) {
        return true;
    }

    llama_batch batch =
        llama_batch_init(
            static_cast<int32_t>(tokens.size()),
            0,
            1
        );

    batch.n_tokens =
        static_cast<int32_t>(tokens.size());

    for (int32_t i = 0; i < batch.n_tokens; ++i) {
        batch.token[i] = tokens[i];

        batch.pos[i] =
            start_pos + i;

        batch.n_seq_id[i] = 1;
        batch.seq_id[i][0] = 0;

        // We only need logits from the final token.
        batch.logits[i] =
            (i == batch.n_tokens - 1);
    }

    int result =
        llama_decode(ctx, batch);

    llama_batch_free(batch);

    if (result != 0) {
        fprintf(
            stderr,
            "ERROR: llama_decode failed: %d\n",
            result
        );

        return false;
    }

    return true;
}

// ============================================================
// Decode one generated token
// ============================================================

static bool decode_token(
    llama_context * ctx,
    llama_token token,
    llama_pos position
) {
    std::vector<llama_token> tokens = { token };

    return decode_tokens(
        ctx,
        tokens,
        position
    );
}

// ============================================================
// Token -> string
// ============================================================

static std::string token_to_string(
    const llama_vocab * vocab,
    llama_token token
) {
    char buffer[512];

    int n = llama_token_to_piece(
        vocab,
        token,
        buffer,
        sizeof(buffer),
        0,
        true
    );

    if (n < 0) {
        std::vector<char> large_buffer(-n);

        n = llama_token_to_piece(
            vocab,
            token,
            large_buffer.data(),
            large_buffer.size(),
            0,
            true
        );

        if (n < 0) {
            return {};
        }

        return std::string(
            large_buffer.data(),
            n
        );
    }

    return std::string(
        buffer,
        n
    );
}

// ============================================================
// Trim output
// ============================================================

static std::string trim(std::string text) {
    auto first = std::find_if(
        text.begin(),
        text.end(),
        [](unsigned char c) {
            return !std::isspace(c);
        }
    );

    auto last = std::find_if(
        text.rbegin(),
        text.rend(),
        [](unsigned char c) {
            return !std::isspace(c);
        }
    ).base();

    if (first >= last) {
        return {};
    }

    return std::string(first, last);
}

// ============================================================
// Normalize one piece of text
// ============================================================

static std::string normalize(
    llama_context * ctx,
    const llama_vocab * vocab,
    llama_sampler * sampler,
    int prefix_tokens,
    const std::vector<uint8_t> & prefix_state,
    const std::string & text
) {
    // --------------------------------------------------------
    // Restore the permanent prompt checkpoint. LFM2.5 has recurrent
    // memory, so it cannot reliably remove only a dynamic suffix.
    // --------------------------------------------------------

    llama_memory_t memory =
        llama_get_memory(ctx);

    llama_memory_clear(memory, true);

    if (llama_state_seq_set_data(
            ctx,
            prefix_state.data(),
            prefix_state.size(),
            0
        ) != prefix_state.size()) {

        fprintf(
            stderr,
            "ERROR: failed to restore cached prompt state\n"
        );

        return {};
    }

    llama_sampler_reset(sampler);

    // --------------------------------------------------------
    // Only this small part changes for every transcription.
    // --------------------------------------------------------

    std::string request =
        "Input: " + text + "\n"
        "Output:";

    std::vector<llama_token> request_tokens =
        tokenize(
            vocab,
            request,
            false
        );

    if (request_tokens.empty()) {
        return {};
    }

    // Fixed prompt already occupies positions:
    //
    //   0 ... prefix_tokens - 1
    //
    // So dynamic input starts exactly at prefix_tokens.

    if (!decode_tokens(
            ctx,
            request_tokens,
            prefix_tokens
        )) {

        return {};
    }

    llama_pos next_position =
        prefix_tokens +
        static_cast<llama_pos>(
            request_tokens.size()
        );

    // --------------------------------------------------------
    // Generate normalized text
    // --------------------------------------------------------

    std::string output;

    for (int i = 0; i < MAX_OUTPUT_TOKENS; ++i) {

        llama_token token =
            llama_sampler_sample(
                sampler,
                ctx,
                -1
            );

        if (llama_vocab_is_eog(
                vocab,
                token
            )) {

            break;
        }

        std::string piece =
            token_to_string(
                vocab,
                token
            );

        // Stop at the first line.
        // We only want the normalized result, not another
        // "Input:" example generated by the model.

        size_t newline =
            piece.find_first_of("\r\n");

        if (newline != std::string::npos) {
            output.append(
                piece.data(),
                newline
            );

            break;
        }

        output += piece;

        // Feed generated token back into the model so it can
        // generate the next one.

        if (!decode_token(
                ctx,
                token,
                next_position
            )) {

            break;
        }

        ++next_position;
    }

    return trim(output);
}

// ============================================================
// Main
// ============================================================

int main(int argc, char ** argv) {
    if (argc < 2) {
        printf(
            "Usage:\n"
            "  test-llama.exe \"text to normalize\"\n"
            "\n"
            "Example:\n"
            "  test-llama.exe \"github dot com\"\n"
        );

        return 1;
    }

    // --------------------------------------------------------
    // Initialize llama
    // --------------------------------------------------------

    llama_backend_init();
    ggml_backend_load_all();

    // --------------------------------------------------------
    // Load model
    // --------------------------------------------------------

    llama_model_params model_params =
        llama_model_default_params();

    // CPU only
    model_params.n_gpu_layers = 0;

    llama_model * model =
        llama_model_load_from_file(
            MODEL_PATH,
            model_params
        );

    if (!model) {
        fprintf(
            stderr,
            "ERROR: failed to load model:\n%s\n",
            MODEL_PATH
        );

        llama_backend_free();
        return 1;
    }

    const llama_vocab * vocab =
        llama_model_get_vocab(model);

    // --------------------------------------------------------
    // Tokenize permanent prompt
    // --------------------------------------------------------

    std::vector<llama_token> prefix =
        tokenize(
            vocab,
            NORMALIZATION_PROMPT,
            true
        );

    if (prefix.empty()) {
        fprintf(
            stderr,
            "ERROR: failed to tokenize normalization prompt\n"
        );

        llama_model_free(model);
        llama_backend_free();

        return 1;
    }

    // --------------------------------------------------------
    // Create context
    // --------------------------------------------------------

    llama_context_params ctx_params =
        llama_context_default_params();

    ctx_params.n_ctx   = 2048;
    ctx_params.n_batch = 1024;

    llama_context * ctx =
        llama_init_from_model(
            model,
            ctx_params
        );

    if (!ctx) {
        fprintf(
            stderr,
            "ERROR: failed to create llama context\n"
        );

        llama_model_free(model);
        llama_backend_free();

        return 1;
    }

    // --------------------------------------------------------
    // Evaluate permanent prompt ONCE.
    //
    // After this call its attention state remains in the
    // llama context memory / KV cache.
    // --------------------------------------------------------

    if (!decode_tokens(
            ctx,
            prefix,
            0
        )) {

        fprintf(
            stderr,
            "ERROR: failed to cache normalization prompt\n"
        );

        llama_free(ctx);
        llama_model_free(model);
        llama_backend_free();

        return 1;
    }

    const int prefix_tokens =
        static_cast<int>(prefix.size());

    std::vector<uint8_t> prefix_state(
        llama_state_seq_get_size(ctx, 0)
    );

    if (
        prefix_state.empty() ||
        llama_state_seq_get_data(
            ctx,
            prefix_state.data(),
            prefix_state.size(),
            0
        ) != prefix_state.size()
    ) {
        fprintf(stderr, "ERROR: failed to snapshot normalization prompt\n");
        llama_free(ctx);
        llama_model_free(model);
        llama_backend_free();
        return 1;
    }

    fprintf(
        stderr,
        "Normalization prompt cached: %d tokens\n",
        prefix_tokens
    );

    // --------------------------------------------------------
    // Greedy sampler
    //
    // Normalization should be deterministic.
    // --------------------------------------------------------

    llama_sampler_chain_params sampler_params =
        llama_sampler_chain_default_params();

    llama_sampler * sampler =
        llama_sampler_chain_init(
            sampler_params
        );

    llama_sampler_chain_add(
        sampler,
        llama_sampler_init_greedy()
    );

    // --------------------------------------------------------
    // Normalize every command-line argument.
    //
    // Usually you'll pass one argument.
    // Multiple arguments let us demonstrate real KV reuse.
    // --------------------------------------------------------

    for (int i = 1; i < argc; ++i) {

        std::string input = argv[i];

        std::string output =
            normalize(
                ctx,
                vocab,
                sampler,
                prefix_tokens,
                prefix_state,
                input
            );

        printf("%s\n", output.c_str());
    }

    // --------------------------------------------------------
    // Cleanup
    // --------------------------------------------------------

    llama_sampler_free(sampler);
    llama_free(ctx);
    llama_model_free(model);
    llama_backend_free();

    return 0;
}
