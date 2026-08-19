#include "LlmPostprocessor.h"

#include "ggml-backend.h"
#include "llama.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <vector>

namespace {

constexpr int kMaximumOutputTokens = 128;

// This fixed prefix is decoded once when the model is loaded. Every dictation
// reuses its KV entries and replaces only the short Input/Output suffix.
constexpr const char* kNormalizationPrompt =
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

std::vector<llama_token> tokenize(
    const llama_vocab* vocabulary,
    const std::string& text,
    bool add_special
) {
    int count = llama_tokenize(
        vocabulary,
        text.c_str(),
        static_cast<int32_t>(text.size()),
        nullptr,
        0,
        add_special,
        false
    );

    if (count >= 0) {
        return {};
    }

    std::vector<llama_token> tokens(static_cast<std::size_t>(-count));
    count = llama_tokenize(
        vocabulary,
        text.c_str(),
        static_cast<int32_t>(text.size()),
        tokens.data(),
        static_cast<int32_t>(tokens.size()),
        add_special,
        false
    );

    if (count < 0) {
        return {};
    }

    tokens.resize(static_cast<std::size_t>(count));
    return tokens;
}

bool decode_tokens(
    llama_context* context,
    const std::vector<llama_token>& tokens,
    llama_pos start_position
) {
    if (tokens.empty()) {
        return true;
    }

    llama_batch batch = llama_batch_init(
        static_cast<int32_t>(tokens.size()),
        0,
        1
    );
    batch.n_tokens = static_cast<int32_t>(tokens.size());

    for (int32_t i = 0; i < batch.n_tokens; ++i) {
        batch.token[i] = tokens[static_cast<std::size_t>(i)];
        batch.pos[i] = start_position + i;
        batch.n_seq_id[i] = 1;
        batch.seq_id[i][0] = 0;
        batch.logits[i] = i == batch.n_tokens - 1;
    }

    const int result = llama_decode(context, batch);
    llama_batch_free(batch);
    return result == 0;
}

std::string token_piece(
    const llama_vocab* vocabulary,
    llama_token token
) {
    char buffer[512];
    int size = llama_token_to_piece(
        vocabulary,
        token,
        buffer,
        sizeof(buffer),
        0,
        true
    );

    if (size >= 0) {
        return std::string(buffer, static_cast<std::size_t>(size));
    }

    std::vector<char> large_buffer(static_cast<std::size_t>(-size));
    size = llama_token_to_piece(
        vocabulary,
        token,
        large_buffer.data(),
        static_cast<int32_t>(large_buffer.size()),
        0,
        true
    );

    return size < 0
        ? std::string{}
        : std::string(large_buffer.data(), static_cast<std::size_t>(size));
}

std::string trim(std::string text) {
    const auto first = std::find_if_not(
        text.begin(),
        text.end(),
        [](unsigned char character) { return std::isspace(character); }
    );
    const auto last = std::find_if_not(
        text.rbegin(),
        text.rend(),
        [](unsigned char character) { return std::isspace(character); }
    ).base();

    return first < last ? std::string(first, last) : std::string{};
}

}

struct LlmPostprocessor::State {
    llama_model* model = nullptr;
    llama_context* context = nullptr;
    llama_sampler* sampler = nullptr;
    const llama_vocab* vocabulary = nullptr;
    llama_pos prefix_tokens = 0;
    std::vector<uint8_t> prefix_state;
};

LlmPostprocessor::LlmPostprocessor() = default;

LlmPostprocessor::~LlmPostprocessor() {
    shutdown();
}

bool LlmPostprocessor::initialize(
    const std::filesystem::path& model_path,
    bool enabled,
    std::string& error
) {
    std::lock_guard<std::mutex> lock(mutex_);
    error.clear();
    model_path_ = model_path;
    enabled_ = enabled;

    if (!enabled_) {
        unload_locked();
        return true;
    }

    return load_locked(error);
}

bool LlmPostprocessor::set_enabled(bool enabled, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    error.clear();

    if (!enabled) {
        enabled_ = false;
        unload_locked();
        return true;
    }

    enabled_ = true;
    if (load_locked(error)) {
        return true;
    }

    enabled_ = false;
    return false;
}

bool LlmPostprocessor::load_locked(std::string& error) {
    if (state_) {
        return true;
    }

    if (model_path_.empty()) {
        error =
            "The LLM model was not found. Put LFM2.5-350M-Q8_0.gguf in "
            "models, pass --llm-model, or set LOQEL_LLM_MODEL.";
        return false;
    }

    if (!backend_initialized_) {
        llama_backend_init();
        ggml_backend_load_all();
        backend_initialized_ = true;
    }

    auto state = std::make_unique<State>();
    llama_model_params model_parameters = llama_model_default_params();
    model_parameters.n_gpu_layers = 0;

    const std::string path = model_path_.u8string();
    state->model = llama_model_load_from_file(path.c_str(), model_parameters);
    if (!state->model) {
        error = "Could not load the LLM model: " + path;
        return false;
    }

    state->vocabulary = llama_model_get_vocab(state->model);
    const std::vector<llama_token> prefix = tokenize(
        state->vocabulary,
        kNormalizationPrompt,
        true
    );
    if (prefix.empty()) {
        error = "Could not tokenize the LLM correction prompt.";
        llama_model_free(state->model);
        return false;
    }

    llama_context_params context_parameters = llama_context_default_params();
    context_parameters.n_ctx = 2048;
    context_parameters.n_batch = 1024;
    state->context = llama_init_from_model(state->model, context_parameters);
    if (!state->context) {
        error = "Could not create the LLM correction context.";
        llama_model_free(state->model);
        return false;
    }

    if (!decode_tokens(state->context, prefix, 0)) {
        error = "Could not cache the LLM correction prompt.";
        llama_free(state->context);
        llama_model_free(state->model);
        return false;
    }

    state->prefix_tokens = static_cast<llama_pos>(prefix.size());

    const std::size_t prefix_state_size = llama_state_seq_get_size(
        state->context,
        0
    );
    if (prefix_state_size == 0) {
        error = "Could not size the cached LLM correction prompt state.";
        llama_free(state->context);
        llama_model_free(state->model);
        return false;
    }

    state->prefix_state.resize(prefix_state_size);
    const std::size_t copied = llama_state_seq_get_data(
        state->context,
        state->prefix_state.data(),
        state->prefix_state.size(),
        0
    );
    if (copied != state->prefix_state.size()) {
        error = "Could not snapshot the cached LLM correction prompt.";
        llama_free(state->context);
        llama_model_free(state->model);
        return false;
    }

    state->sampler = llama_sampler_chain_init(
        llama_sampler_chain_default_params()
    );
    llama_sampler_chain_add(state->sampler, llama_sampler_init_greedy());
    state_ = std::move(state);
    return true;
}

void LlmPostprocessor::unload_locked() {
    if (!state_) {
        return;
    }

    llama_sampler_free(state_->sampler);
    llama_free(state_->context);
    llama_model_free(state_->model);
    state_.reset();
}

bool LlmPostprocessor::process(
    const std::string& text,
    std::string& output,
    std::string& error
) {
    std::lock_guard<std::mutex> lock(mutex_);
    output = text;
    error.clear();

    if (!enabled_ || text.empty()) {
        return true;
    }

    if (!state_) {
        error = "The LLM postprocessor is enabled but not loaded.";
        return false;
    }

    llama_memory_t memory = llama_get_memory(state_->context);
    llama_memory_clear(memory, true);

    const std::size_t restored = llama_state_seq_set_data(
        state_->context,
        state_->prefix_state.data(),
        state_->prefix_state.size(),
        0
    );
    if (restored != state_->prefix_state.size()) {
        error = "Could not restore the cached LLM correction prompt.";
        return false;
    }

    llama_sampler_reset(state_->sampler);
    const std::string request = "Input: " + text + "\nOutput:";
    const std::vector<llama_token> request_tokens = tokenize(
        state_->vocabulary,
        request,
        false
    );
    if (request_tokens.empty()) {
        error = "Could not tokenize the text for LLM correction.";
        return false;
    }

    if (!decode_tokens(
            state_->context,
            request_tokens,
            state_->prefix_tokens
        )) {
        error = "Could not evaluate the text for LLM correction.";
        return false;
    }

    llama_pos next_position = state_->prefix_tokens +
        static_cast<llama_pos>(request_tokens.size());
    std::string corrected;

    for (int i = 0; i < kMaximumOutputTokens; ++i) {
        const llama_token token = llama_sampler_sample(
            state_->sampler,
            state_->context,
            -1
        );

        if (llama_vocab_is_eog(state_->vocabulary, token)) {
            break;
        }

        const std::string piece = token_piece(state_->vocabulary, token);
        const std::size_t newline = piece.find_first_of("\r\n");
        if (newline != std::string::npos) {
            corrected.append(piece.data(), newline);
            break;
        }

        corrected += piece;
        if (!decode_tokens(state_->context, {token}, next_position++)) {
            error = "Could not generate the complete LLM correction.";
            return false;
        }
    }

    corrected = trim(std::move(corrected));
    if (corrected.empty()) {
        error = "The LLM returned an empty correction.";
        return false;
    }

    output = std::move(corrected);
    return true;
}

void LlmPostprocessor::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    enabled_ = false;
    unload_locked();

    if (backend_initialized_) {
        llama_backend_free();
        backend_initialized_ = false;
    }
}

bool LlmPostprocessor::enabled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return enabled_;
}

bool LlmPostprocessor::loaded() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_ != nullptr;
}
