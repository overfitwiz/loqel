#include "LlmPostprocessor.h"

#include "ggml-backend.h"
#include "llama.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

namespace {

constexpr int32_t kContextTokens = 2048;

// This static prefix is formatted with the model's own chat template, evaluated
// once, and cached after loading. Examples live here rather than in the dynamic
// user message so they do not add per-dictation prompt evaluation work.
constexpr const char* kSystemPrompt =
    "You are a speech-to-text normalization engine. Convert clearly spoken "
    "written forms, including URLs, email addresses, currencies, numbers, "
    "decimals, percentages, dates, times, phone numbers, units, and symbols. "
    "In URLs and email addresses, always convert spoken dot, slash, colon, at, "
    "dash, underscore, and plus to their symbols. Preserve every sentence, "
    "clause, repetition, negation, uncertain fragment, and line break. A "
    "normalization may be much shorter or longer than its spoken form. Never "
    "summarize, omit an idea, explain, or add information. If the intended "
    "form is uncertain, leave it unchanged. Return only normalized text.\n\n"
    "Examples:\n"
    "Input: Go to github dot com. Keep this sentence.\n"
    "Output: Go to github.com. Keep this sentence.\n\n"
    "Input: Open github dot com slash pricing. Then leave this sentence unchanged.\n"
    "Output: Open github.com/pricing. Then leave this sentence unchanged.\n\n"
    "Input: Visit https colon slash slash docs dot example dot org slash v2.\n"
    "Output: Visit https://docs.example.org/v2.\n\n"
    "Input: Email jane dot doe at example dot com.\n"
    "Output: Email jane.doe@example.com.\n\n"
    "Input: Write to support dash eu at my underscore company dot co dot uk.\n"
    "Output: Write to support-eu@my_company.co.uk.\n\n"
    "Input: It costs twenty five dollars and fifty cents.\n"
    "Output: It costs $25.50.\n\n"
    "Input: The refund was one thousand two hundred euros.\n"
    "Output: The refund was EUR 1,200.\n\n"
    "Input: Order three hundred and forty two items.\n"
    "Output: Order 342 items.\n\n"
    "Input: Use version two point five and set the ratio to one point two five.\n"
    "Output: Use version 2.5 and set the ratio to 1.25.\n\n"
    "Input: Growth was twelve point five percent.\n"
    "Output: Growth was 12.5%.\n\n"
    "Input: Call plus one four one five five five five zero one two three.\n"
    "Output: Call +1 415 555 0123.\n\n"
    "Input: Meet on August nineteenth at three thirty P M.\n"
    "Output: Meet on August 19th at 3:30 PM.\n\n"
    "Input: The file is twenty four megabytes and the limit is ten kilograms.\n"
    "Output: The file is 24 MB and the limit is 10 kg.\n\n"
    "Input: Keep this first line exactly.\nKeep this second line too.\n"
    "Output: Keep this first line exactly.\nKeep this second line too.";

constexpr const char* kUserInstruction =
    "Correct this transcript. Preserve all content and return only corrected text:\n";

std::vector<llama_chat_message> static_messages() {
    return {
        {"system", kSystemPrompt}
    };
}

bool format_chat(
    const std::string& chat_template,
    const std::vector<llama_chat_message>& messages,
    bool add_assistant,
    std::string& formatted
) {
    const int32_t size = llama_chat_apply_template(
        chat_template.c_str(),
        messages.data(),
        messages.size(),
        add_assistant,
        nullptr,
        0
    );
    if (size <= 0) {
        return false;
    }

    std::vector<char> buffer(static_cast<std::size_t>(size) + 1);
    const int32_t written = llama_chat_apply_template(
        chat_template.c_str(),
        messages.data(),
        messages.size(),
        add_assistant,
        buffer.data(),
        static_cast<int32_t>(buffer.size())
    );
    if (written <= 0 || written > size) {
        return false;
    }

    formatted.assign(buffer.data(), static_cast<std::size_t>(written));
    return true;
}

std::vector<llama_token> tokenize(
    const llama_vocab* vocabulary,
    const std::string& text,
    bool add_special,
    bool parse_special
) {
    int count = llama_tokenize(
        vocabulary,
        text.c_str(),
        static_cast<int32_t>(text.size()),
        nullptr,
        0,
        add_special,
        parse_special
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
        parse_special
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
    std::string chat_template;
    std::string formatted_prefix;
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
    const char* chat_template = llama_model_chat_template(state->model, nullptr);
    if (!chat_template) {
        error = "The LLM model does not contain a supported chat template.";
        llama_model_free(state->model);
        return false;
    }
    state->chat_template = chat_template;

    if (!format_chat(
            state->chat_template,
            static_messages(),
            false,
            state->formatted_prefix
        )) {
        error = "Could not apply the LLM model's chat template.";
        llama_model_free(state->model);
        return false;
    }

    const std::vector<llama_token> prefix = tokenize(
        state->vocabulary,
        state->formatted_prefix,
        false,
        true
    );
    if (prefix.empty()) {
        error = "Could not tokenize the LLM correction prompt.";
        llama_model_free(state->model);
        return false;
    }

    llama_context_params context_parameters = llama_context_default_params();
    context_parameters.n_ctx = kContextTokens;
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
    // Liquid AI recommends low-temperature top-k sampling for LFM2.5. A fixed
    // seed keeps correction reproducible while avoiding greedy copy bias.
    llama_sampler_chain_add(state->sampler, llama_sampler_init_top_k(50));
    llama_sampler_chain_add(state->sampler, llama_sampler_init_temp(0.1f));
    llama_sampler_chain_add(state->sampler, llama_sampler_init_dist(0x10ce1u));
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

    std::vector<llama_chat_message> messages = static_messages();
    const std::string user_message = kUserInstruction + text;
    messages.push_back({"user", user_message.c_str()});

    std::string formatted_request;
    if (!format_chat(
            state_->chat_template,
            messages,
            true,
            formatted_request
        )) {
        error = "Could not apply the LLM model's chat template.";
        return false;
    }
    if (
        formatted_request.size() < state_->formatted_prefix.size() ||
        formatted_request.compare(
            0,
            state_->formatted_prefix.size(),
            state_->formatted_prefix
        ) != 0
    ) {
        error = "The LLM chat template produced an inconsistent prompt.";
        return false;
    }

    const std::string request = formatted_request.substr(
        state_->formatted_prefix.size()
    );
    const std::vector<llama_token> request_tokens = tokenize(
        state_->vocabulary,
        request,
        false,
        true
    );
    if (request_tokens.empty()) {
        error = "Could not tokenize the text for LLM correction.";
        return false;
    }

    const std::vector<llama_token> input_tokens = tokenize(
        state_->vocabulary,
        text,
        false,
        false
    );
    const int64_t available_tokens =
        static_cast<int64_t>(kContextTokens) -
        static_cast<int64_t>(state_->prefix_tokens) -
        static_cast<int64_t>(request_tokens.size());

    // If there is not enough context to return text of approximately the same
    // size, bypass correction. The recognized transcript remains in output.
    if (
        input_tokens.empty() ||
        available_tokens < static_cast<int64_t>(input_tokens.size()) + 16
    ) {
        return true;
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
    const int64_t desired_output_tokens =
        static_cast<int64_t>(input_tokens.size()) +
        std::max<int64_t>(64, static_cast<int64_t>(input_tokens.size()) / 2);
    const int maximum_output_tokens = static_cast<int>(
        std::min(available_tokens, desired_output_tokens)
    );
    bool completed = false;

    for (int i = 0; i < maximum_output_tokens; ++i) {
        const llama_token token = llama_sampler_sample(
            state_->sampler,
            state_->context,
            -1
        );

        if (llama_vocab_is_eog(state_->vocabulary, token)) {
            completed = true;
            break;
        }

        const std::string piece = token_piece(state_->vocabulary, token);
        corrected += piece;
        if (!decode_tokens(state_->context, {token}, next_position++)) {
            error = "Could not generate the complete LLM correction.";
            return false;
        }
    }

    corrected = trim(std::move(corrected));
    if (!completed || corrected.empty()) {
        // A missing end token indicates token/context truncation. An empty
        // answer is unusable. In either case retain the original transcript
        // and treat optional correction as bypassed. Completed non-empty
        // answers are accepted without comparing their length to the input.
        return true;
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
