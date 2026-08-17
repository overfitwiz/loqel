#include "AsrEngine.h"

#include <algorithm>

AsrEngine::~AsrEngine() {
    unload();
}

void AsrEngine::unload() {
    close_stream();

    if (recognizer_) {
        nemo_speech_asr_destroy(
            recognizer_
        );

        recognizer_ = nullptr;
    }

    model_path_.clear();
    model_file_.clear();
}

std::string AsrEngine::make_error(
    const char* operation
) {
    const char* detail =
        nemo_speech_asr_last_error();

    return
        std::string(operation) +
        ": " +
        (detail ? detail : "unknown ASR error");
}

bool AsrEngine::load(
    const std::filesystem::path& model_path,
    int rnnt_right_context,
    std::string& error
) {
    unload();

    model_file_ = model_path;
    model_path_ =
        model_path.u8string();
    rnnt_right_context_ = rnnt_right_context;

    nemo_speech_asr_backend_config backend = {};

    backend.size =
        sizeof(backend);

    // CPU only.
    backend.gpu = -1;


    nemo_speech_asr_model_config model = {};

    model.size =
        sizeof(model);

    model.path =
        model_path_.c_str();


    nemo_speech_asr_streaming_config streaming = {};

    streaming.size =
        sizeof(streaming);

    streaming.chunk_size =
        0.16f;

    streaming.ctc_left_padding =
        1.92f;

    streaming.ctc_right_padding =
        1.92f;

    streaming.rnnt_right_context =
        rnnt_right_context_;


    nemo_speech_asr_endpointing_config endpointing = {};

    endpointing.size =
        sizeof(endpointing);

    endpointing.enable =
        true;

    endpointing.vad_based =
        false;

    endpointing.stop_history_eou_ms =
        600;


    nemo_speech_asr_recognizer_config config = {};

    config.size =
        sizeof(config);

    config.backend =
        &backend;

    config.model =
        &model;

    config.streaming =
        &streaming;

    config.endpointing =
        &endpointing;


    if (
        nemo_speech_asr_create(
            &config,
            &recognizer_
        ) != NEMO_SPEECH_ASR_OK
    ) {
        error =
            make_error(
                "Loading ASR model"
            );

        return false;
    }



    return true;
}

bool AsrEngine::reconfigure_streaming(
    int rnnt_right_context,
    std::string& error
) {
    if (!recognizer_) {
        error = "ASR model is not loaded.";
        return false;
    }

    if (rnnt_right_context == rnnt_right_context_) {
        return true;
    }

    const std::filesystem::path model_path = model_file_;
    const int previous_right_context = rnnt_right_context_;

    if (load(model_path, rnnt_right_context, error)) {
        return true;
    }

    const std::string requested_error = error;
    std::string restore_error;

    if (!load(model_path, previous_right_context, restore_error)) {
        error =
            requested_error +
            "; restoring the previous latency also failed: " +
            restore_error;
    }
    else {
        error = requested_error;
    }

    return false;
}

bool AsrEngine::loaded() const {
    return recognizer_ != nullptr;
}

int AsrEngine::rnnt_right_context() const {
    return rnnt_right_context_;
}

bool AsrEngine::start_stream(
    const std::vector<std::string>& speech_contexts,
    float speech_context_boost,
    std::string& error
) {
    if (!recognizer_) {
        error =
            "ASR model is not loaded.";

        return false;
    }

    close_stream();

    nemo_speech_asr_recognition_options options =
        nemo_speech_asr_recognition_options_default();

    options.interim_results =
        true;

    options.enable_automatic_punctuation =
        true;

    std::vector<const char*> phrase_pointers;

    phrase_pointers.reserve(
        speech_contexts.size()
    );

    for (const std::string& phrase : speech_contexts) {
        if (!phrase.empty()) {
            phrase_pointers.push_back(
                phrase.c_str()
            );
        }
    }

    nemo_speech_asr_speech_context context = {};

    if (
        !phrase_pointers.empty() &&
        speech_context_boost > 0.0f
    ) {
        context.size = sizeof(context);
        context.phrases = phrase_pointers.data();
        context.phrase_count = phrase_pointers.size();
        context.boost = std::min(
            speech_context_boost,
            5.0f
        );

        options.speech_contexts = &context;
        options.speech_context_count = 1;
    }

    if (
        nemo_speech_asr_streaming_recognize(
            recognizer_,
            &options,
            &stream_
        ) != NEMO_SPEECH_ASR_OK
    ) {
        error =
            make_error(
                "Creating ASR stream"
            );

        return false;
    }

    return true;
}

bool AsrEngine::push(
    const std::vector<float>& samples,
    int sample_rate,
    std::vector<AsrResult>& results,
    std::string& error
) {
    if (!stream_) {
        error =
            "ASR stream does not exist.";

        return false;
    }

    if (samples.empty()) {
        return true;
    }

    if (
        nemo_speech_asr_stream_push_f32(
            stream_,
            samples.data(),
            samples.size(),
            sample_rate
        ) != NEMO_SPEECH_ASR_OK
    ) {
        error =
            make_error(
                "Sending audio to ASR"
            );

        return false;
    }

    return drain_results(
        results,
        error
    );
}

bool AsrEngine::drain_results(
    std::vector<AsrResult>& results,
    std::string& error
) {
    for (;;) {
        nemo_speech_asr_result* result =
            nullptr;

        const auto status =
            nemo_speech_asr_stream_next(
                stream_,
                &result
            );

        if (
            status !=
            NEMO_SPEECH_ASR_OK
        ) {
            error =
                make_error(
                    "Reading ASR results"
                );

            return false;
        }

        if (!result) {
            return true;
        }

        const char* transcript =
            nemo_speech_asr_result_transcript(
                result,
                0
            );

        AsrResult update;

        update.text =
            transcript
                ? transcript
                : "";

        update.is_final =
            nemo_speech_asr_result_is_final(
                result
            );

        results.push_back(
            std::move(update)
        );

        nemo_speech_asr_result_destroy(
            result
        );
    }
}

bool AsrEngine::finish(
    std::vector<AsrResult>& results,
    std::string& error
) {
    if (!stream_) {
        return true;
    }

    if (
        nemo_speech_asr_stream_finish(
            stream_
        ) != NEMO_SPEECH_ASR_OK
    ) {
        error =
            make_error(
                "Finalizing ASR stream"
            );

        close_stream();

        return false;
    }

    const bool ok =
        drain_results(
            results,
            error
        );

    close_stream();

    return ok;
}

bool AsrEngine::recognize(
    const std::vector<float>& samples,
    int sample_rate,
    const std::vector<std::string>& speech_contexts,
    float speech_context_boost,
    std::string& transcript,
    std::string& error
) {
    transcript.clear();

    if (!recognizer_) {
        error = "ASR model is not loaded.";
        return false;
    }

    if (samples.empty()) {
        return true;
    }

    close_stream();

    nemo_speech_asr_recognition_options options =
        nemo_speech_asr_recognition_options_default();

    options.enable_automatic_punctuation = true;
    options.interim_results = false;

    std::vector<const char*> phrase_pointers;
    phrase_pointers.reserve(speech_contexts.size());

    for (const std::string& phrase : speech_contexts) {
        if (!phrase.empty()) {
            phrase_pointers.push_back(phrase.c_str());
        }
    }

    nemo_speech_asr_speech_context context = {};
    if (!phrase_pointers.empty() && speech_context_boost > 0.0f) {
        context.size = sizeof(context);
        context.phrases = phrase_pointers.data();
        context.phrase_count = phrase_pointers.size();
        context.boost = std::min(speech_context_boost, 5.0f);
        options.speech_contexts = &context;
        options.speech_context_count = 1;
    }

    nemo_speech_asr_result* result = nullptr;
    const auto status = nemo_speech_asr_recognize_f32(
        recognizer_,
        &options,
        samples.data(),
        samples.size(),
        sample_rate,
        &result
    );

    if (status != NEMO_SPEECH_ASR_OK || !result) {
        error = make_error("Transcribing recorded audio");

        if (result) {
            nemo_speech_asr_result_destroy(result);
        }

        return false;
    }

    const char* text = nemo_speech_asr_result_transcript(result, 0);
    transcript = text ? text : "";
    nemo_speech_asr_result_destroy(result);
    return true;
}

void AsrEngine::close_stream() {
    if (stream_) {
        nemo_speech_asr_stream_close(
            stream_
        );

        stream_ = nullptr;
    }
}
