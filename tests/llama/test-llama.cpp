#include "llama.h"
#include "ggml-backend.h"

#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char ** argv) {
    if (argc < 2) {
        printf("Usage: test-llama.exe models\\model.gguf\n");
        return 1;
    }

    const std::string model_path = argv[1];

    // --------------------------------------------------------
    // Initialize llama / ggml
    // --------------------------------------------------------

    llama_backend_init();
    ggml_backend_load_all();

    // --------------------------------------------------------
    // Load model
    // --------------------------------------------------------

    llama_model_params model_params = llama_model_default_params();

    // CPU only
    model_params.n_gpu_layers = 0;

    llama_model * model =
        llama_model_load_from_file(model_path.c_str(), model_params);

    if (!model) {
        fprintf(stderr, "ERROR: failed to load model\n");
        return 1;
    }

    printf("SUCCESS: model loaded\n\n");

    const llama_vocab * vocab = llama_model_get_vocab(model);

    // --------------------------------------------------------
    // Tiny formatting prompt
    // --------------------------------------------------------

    const std::string prompt =
        "You are a text formatter. "
        "Convert spoken formatting into normal written text. "
        "Return ONLY the corrected text. Do not explain.\n"
        "\n"
        "Input: example dot com\n"
        "Output: example.com\n"
        "\n"
        "Input: github dot com\n"
        "Output:";

    printf("Input:\n");
    printf("github dot com\n\n");

    // --------------------------------------------------------
    // Tokenize prompt
    // --------------------------------------------------------

    const int n_prompt = -llama_tokenize(
        vocab,
        prompt.c_str(),
        prompt.size(),
        nullptr,
        0,
        true,
        true
    );

    if (n_prompt <= 0) {
        fprintf(stderr, "ERROR: failed to determine token count\n");
        llama_model_free(model);
        llama_backend_free();
        return 1;
    }

    std::vector<llama_token> prompt_tokens(n_prompt);

    if (llama_tokenize(
            vocab,
            prompt.c_str(),
            prompt.size(),
            prompt_tokens.data(),
            prompt_tokens.size(),
            true,
            true
        ) < 0) {

        fprintf(stderr, "ERROR: failed to tokenize prompt\n");
        llama_model_free(model);
        llama_backend_free();
        return 1;
    }

    // --------------------------------------------------------
    // Create context
    // --------------------------------------------------------

    llama_context_params ctx_params = llama_context_default_params();

    ctx_params.n_ctx   = 512;
    ctx_params.n_batch = 512;

    llama_context * ctx = llama_init_from_model(model, ctx_params);

    if (!ctx) {
        fprintf(stderr, "ERROR: failed to create context\n");
        llama_model_free(model);
        llama_backend_free();
        return 1;
    }

    // --------------------------------------------------------
    // Greedy sampler
    //
    // Greedy is useful here because formatting should be
    // deterministic rather than creative.
    // --------------------------------------------------------

    llama_sampler_chain_params sampler_params =
        llama_sampler_chain_default_params();

    llama_sampler * sampler =
        llama_sampler_chain_init(sampler_params);

    llama_sampler_chain_add(
        sampler,
        llama_sampler_init_greedy()
    );

    // --------------------------------------------------------
    // Feed prompt into model
    // --------------------------------------------------------

    llama_batch batch =
        llama_batch_get_one(
            prompt_tokens.data(),
            static_cast<int32_t>(prompt_tokens.size())
        );

    if (llama_decode(ctx, batch) != 0) {
        fprintf(stderr, "ERROR: failed to decode prompt\n");

        llama_sampler_free(sampler);
        llama_free(ctx);
        llama_model_free(model);
        llama_backend_free();

        return 1;
    }

    // --------------------------------------------------------
    // Generate
    // --------------------------------------------------------

    constexpr int MAX_GENERATED_TOKENS = 32;

    std::string output;

    for (int i = 0; i < MAX_GENERATED_TOKENS; ++i) {

        // Pick next token
        llama_token token =
            llama_sampler_sample(sampler, ctx, -1);

        // Stop at end-of-generation
        if (llama_vocab_is_eog(vocab, token)) {
            break;
        }

        // Convert token -> text
        char buffer[256];

        int length = llama_token_to_piece(
            vocab,
            token,
            buffer,
            sizeof(buffer),
            0,
            true
        );

        if (length < 0) {
            fprintf(stderr, "ERROR: failed to convert token to text\n");
            break;
        }

        output.append(buffer, length);

        // Feed generated token back into the model
        batch = llama_batch_get_one(&token, 1);

        if (llama_decode(ctx, batch) != 0) {
            fprintf(stderr, "ERROR: failed during generation\n");
            break;
        }
    }

    // --------------------------------------------------------
    // Result
    // --------------------------------------------------------

    printf("Output:\n");
    printf("%s\n", output.c_str());

    // --------------------------------------------------------
    // Cleanup
    // --------------------------------------------------------

    llama_sampler_free(sampler);
    llama_free(ctx);
    llama_model_free(model);
    llama_backend_free();

    return 0;
}