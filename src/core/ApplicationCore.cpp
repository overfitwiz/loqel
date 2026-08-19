#include "ApplicationCore.h"

#include <cctype>
#include <iterator>
#include <utility>
#include <vector>

namespace {

void append_utterance(
    std::string& committed,
    const std::string& text
) {
    if (text.empty()) {
        return;
    }

    if (!committed.empty()) {
        committed.push_back(' ');
    }

    committed += text;
}

bool is_word_byte(unsigned char character) {
    return character >= 0x80 || std::isalnum(character) || character == '_';
}

bool is_space_byte(unsigned char character) {
    return character < 0x80 && std::isspace(character);
}

bool is_punctuation(char character) {
    return
        character == '.' || character == ',' || character == ';' ||
        character == ':' || character == '!' || character == '?';
}

char ascii_lower(char character) {
    const auto byte = static_cast<unsigned char>(character);
    return byte < 0x80
        ? static_cast<char>(std::tolower(byte))
        : character;
}

bool matches_cleanup_phrase(
    const std::string& text,
    std::size_t position,
    const std::string& phrase
) {
    if (phrase.empty() || position + phrase.size() > text.size()) {
        return false;
    }

    if (
        position > 0 &&
        is_word_byte(static_cast<unsigned char>(text[position - 1]))
    ) {
        return false;
    }

    const std::size_t end = position + phrase.size();
    if (
        end < text.size() &&
        is_word_byte(static_cast<unsigned char>(text[end]))
    ) {
        return false;
    }

    for (std::size_t i = 0; i < phrase.size(); ++i) {
        if (ascii_lower(text[position + i]) != ascii_lower(phrase[i])) {
            return false;
        }
    }

    return true;
}

std::string remove_cleanup_words(
    const std::string& text,
    const CleanupSettings& cleanup
) {
    std::string filtered;
    filtered.reserve(text.size());

    std::size_t position = 0;
    while (position < text.size()) {
        const std::string* match = nullptr;

        for (const std::string& phrase : cleanup.remove_words) {
            if (
                matches_cleanup_phrase(text, position, phrase) &&
                (!match || phrase.size() > match->size())
            ) {
                match = &phrase;
            }
        }

        if (!match) {
            filtered.push_back(text[position++]);
            continue;
        }

        position += match->size();

        // Avoid leaving doubled punctuation around a removed filler, while
        // retaining sentence punctuation when the filler ended a sentence.
        if (position < text.size() && is_punctuation(text[position])) {
            while (!filtered.empty() && is_space_byte(filtered.back())) {
                filtered.pop_back();
            }

            if (filtered.empty()) {
                while (position < text.size() && is_punctuation(text[position])) {
                    ++position;
                }
            }
            else {
                while (!filtered.empty() && is_punctuation(filtered.back())) {
                    filtered.pop_back();
                }
            }
        }
    }

    std::string result;
    result.reserve(filtered.size());
    bool pending_space = false;

    for (char character : filtered) {
        if (is_space_byte(static_cast<unsigned char>(character))) {
            pending_space = true;
            continue;
        }

        if (
            pending_space && !result.empty() &&
            !is_punctuation(character)
        ) {
            result.push_back(' ');
        }

        result.push_back(character);
        pending_space = false;
    }

    return result;
}

std::string prepare_output(
    const std::string& transcript,
    OutputMode output_mode,
    const CleanupSettings& cleanup,
    const MarkdownCommands& commands
) {
    if (output_mode == OutputMode::Plain) {
        return transcript;
    }

    return MarkdownFormatter::format(
        remove_cleanup_words(transcript, cleanup),
        commands
    );
}

}

ApplicationCore::ApplicationCore(
    IApplicationPlatform& platform,
    IAudioCapture& audio_capture
)
    : platform_(platform),
      audio_capture_(audio_capture) {
}

ApplicationCore::~ApplicationCore() {
    shutdown();
}

bool ApplicationCore::initialize(
    const std::filesystem::path& model_path,
    const std::filesystem::path& llm_model_path,
    std::string& error
) {
    if (!asr_.load(
            model_path,
            rnnt_right_context(recognition_settings_.latency),
            error
        )) {
        return false;
    }

    if (!llm_.initialize(llm_model_path, llm_settings_.enabled, error)) {
        return false;
    }

    return true;
}

void ApplicationCore::set_settings(
    const MarkdownCommands& commands,
    const CustomDictionarySettings& dictionary,
    const RecognitionSettings& recognition,
    const CleanupSettings& cleanup,
    const LlmSettings& llm
) {
    markdown_commands_ = commands;
    custom_dictionary_ = dictionary;
    recognition_settings_ = recognition;
    cleanup_settings_ = cleanup;
    llm_settings_ = llm;

    if (asr_.loaded() && llm_.enabled() != llm.enabled) {
        platform_.show_overlay(
            llm.enabled
                ? "Loading text correction model..."
                : "Unloading text correction model..."
        );

        std::string llm_error;
        const bool ok = llm_.set_enabled(llm.enabled, llm_error);
        platform_.hide_overlay();

        if (!ok) {
            llm_settings_.enabled = false;
            platform_.show_message(
                "Text correction settings",
                llm_error,
                MessageKind::Error
            );
        }
    }

    const int right_context = rnnt_right_context(recognition.latency);
    if (
        asr_.loaded() &&
        !session_active_ &&
        !finalizing_ &&
        asr_.rnnt_right_context() != right_context
    ) {
        platform_.show_overlay("Applying recognition settings...");

        std::string error;
        const bool ok = asr_.reconfigure_streaming(right_context, error);
        platform_.hide_overlay();

        if (!ok) {
            platform_.show_message(
                "Recognition settings",
                error,
                MessageKind::Error
            );
        }
    }
}

void ApplicationCore::start_session(OutputMode output_mode) {
    if (session_active_ || finalizing_ || shutting_down_) {
        return;
    }

    if (!asr_.loaded()) {
        platform_.show_message(
            "loqel",
            "The ASR model is not loaded.",
            MessageKind::Error
        );
        return;
    }

    const int right_context =
        rnnt_right_context(recognition_settings_.latency);

    if (asr_.rnnt_right_context() != right_context) {
        platform_.show_overlay("Applying recognition settings...");

        std::string error;
        if (!asr_.reconfigure_streaming(right_context, error)) {
            platform_.hide_overlay();
            platform_.show_message(
                "Recognition settings",
                error,
                MessageKind::Error
            );
            return;
        }

        platform_.hide_overlay();
    }

    target_ = platform_.capture_active_target();
    active_markdown_commands_ = markdown_commands_;
    active_custom_dictionary_ = custom_dictionary_;
    active_recognition_settings_ = recognition_settings_;
    active_cleanup_settings_ = cleanup_settings_;
    active_output_mode_ = output_mode;
    audio_queue_ = std::make_unique<AudioQueue>();

    if (!audio_capture_.start(*audio_queue_)) {
        std::string error = audio_queue_->error();
        audio_queue_.reset();
        target_ = 0;

        platform_.show_message(
            "Microphone error",
            error.empty() ? "Could not start the microphone." : error,
            MessageKind::Error
        );
        return;
    }

    try {
        consumer_thread_ = std::thread(&ApplicationCore::consume_audio, this);
    }
    catch (...) {
        audio_capture_.stop();
        audio_queue_.reset();
        target_ = 0;
        platform_.show_message(
            "ASR error",
            "Could not start the ASR worker thread.",
            MessageKind::Error
        );
        return;
    }

    session_active_ = true;
    if (active_output_mode_ == OutputMode::Plain) {
        platform_.show_overlay(
            active_recognition_settings_.mode == RecognitionMode::Streaming
                ? "Listening (plain text)..."
                : "Recording (plain text)..."
        );
    }
    else {
        platform_.show_overlay(
            active_recognition_settings_.mode == RecognitionMode::Streaming
                ? "Listening..."
                : "Recording..."
        );
    }
}

void ApplicationCore::stop_session() {
    if (!session_active_ || finalizing_ || shutting_down_) {
        return;
    }

    session_active_ = false;
    finalizing_ = true;
    platform_.show_overlay(
        active_recognition_settings_.mode == RecognitionMode::Streaming
            ? "Finalizing..."
            : "Transcribing..."
    );

    // Stops and joins only the producer. The consumer posts completion after
    // NeMo has drained its final results.
    audio_capture_.stop();
}

void ApplicationCore::consume_audio() {
    SessionResult result;
    AudioQueue* queue = audio_queue_.get();
    int sample_rate = 0;

    if (!queue->wait_until_ready(sample_rate, result.error)) {
        post_session_done(std::move(result));
        return;
    }

    if (
        active_recognition_settings_.mode ==
        RecognitionMode::RecordThenTranscribe
    ) {
        std::vector<float> recording;
        std::vector<float> chunk;

        while (queue->pop(chunk)) {
            recording.insert(
                recording.end(),
                std::make_move_iterator(chunk.begin()),
                std::make_move_iterator(chunk.end())
            );
            chunk.clear();
        }

        result.error = queue->error();

        if (result.error.empty()) {
            std::string transcript;

            if (!asr_.recognize(
                    recording,
                    sample_rate,
                    active_custom_dictionary_.phrases,
                    active_custom_dictionary_.boost,
                    transcript,
                    result.error
                )) {
                transcript.clear();
            }

            result.text = prepare_output(
                transcript,
                active_output_mode_,
                active_cleanup_settings_,
                active_markdown_commands_
            );

            if (
                !llm_.process(result.text, result.text, result.error) &&
                result.error.empty()
            ) {
                result.error = "LLM text correction failed.";
            }
        }

        post_session_done(std::move(result));
        return;
    }

    std::string asr_error;

    if (!asr_.start_stream(
            active_custom_dictionary_.phrases,
            active_custom_dictionary_.boost,
            asr_error
        )) {
        result.error = std::move(asr_error);

        std::vector<float> discard;
        while (queue->pop(discard)) {
            discard.clear();
        }

        post_session_done(std::move(result));
        return;
    }

    std::string committed;
    std::string partial;

    const auto publish = [&] {
        std::string display = committed;

        if (!partial.empty()) {
            if (!display.empty()) {
                display.push_back(' ');
            }
            display += partial;
        }

        display = prepare_output(
            display,
            active_output_mode_,
            active_cleanup_settings_,
            active_markdown_commands_
        );

        platform_.post_to_main(
            [this, display = std::move(display)] {
                if (!shutting_down_ && !display.empty()) {
                    platform_.show_overlay(display);
                }
            }
        );
    };

    const auto process_updates =
        [&](const std::vector<AsrResult>& updates) {
            for (const AsrResult& update : updates) {
                if (update.is_final) {
                    append_utterance(committed, update.text);
                    partial.clear();
                }
                else {
                    partial = update.text;
                }

                publish();
            }
        };

    bool asr_ok = true;
    std::vector<float> samples;

    while (queue->pop(samples)) {
        if (asr_ok) {
            std::vector<AsrResult> updates;

            if (!asr_.push(
                    samples,
                    sample_rate,
                    updates,
                    asr_error
                )) {
                result.error = std::move(asr_error);
                asr_ok = false;
            }
            else {
                process_updates(updates);
            }
        }

        samples.clear();
    }

    const std::string capture_error = queue->error();
    if (result.error.empty() && !capture_error.empty()) {
        result.error = capture_error;
        asr_ok = false;
    }

    if (asr_ok) {
        std::vector<AsrResult> updates;

        if (!asr_.finish(updates, asr_error)) {
            result.error = std::move(asr_error);
        }
        else {
            process_updates(updates);
        }
    }
    else {
        asr_.close_stream();
    }

    result.text = prepare_output(
        committed,
        active_output_mode_,
        active_cleanup_settings_,
        active_markdown_commands_
    );

    if (
        result.error.empty() &&
        !llm_.process(result.text, result.text, result.error) &&
        result.error.empty()
    ) {
        result.error = "LLM text correction failed.";
    }

    post_session_done(std::move(result));
}

void ApplicationCore::post_session_done(SessionResult result) {
    platform_.post_to_main(
        [this, result = std::move(result)]() mutable {
            if (!shutting_down_) {
                handle_session_done(std::move(result));
            }
        }
    );
}

void ApplicationCore::handle_session_done(SessionResult result) {
    audio_capture_.stop();

    if (consumer_thread_.joinable()) {
        consumer_thread_.join();
    }

    audio_queue_.reset();
    session_active_ = false;
    finalizing_ = false;
    platform_.hide_overlay();

    if (!result.error.empty()) {
        platform_.show_message(
            "Dictation error",
            result.error,
            MessageKind::Error
        );
        target_ = 0;
        return;
    }

    if (result.text.empty()) {
        target_ = 0;
        return;
    }

    if (!target_ || !platform_.is_target_active(target_)) {
        platform_.show_message(
            "loqel",
            "The active application changed while dictating, so the text was not inserted.",
            MessageKind::Warning
        );
        target_ = 0;
        return;
    }

    if (active_output_mode_ == OutputMode::Formatted) {
        result.text.push_back(' ');
    }
    std::string insertion_error;

    if (!platform_.insert_text(result.text, insertion_error)) {
        platform_.show_message(
            "Text insertion error",
            insertion_error.empty()
                ? "The operating system did not accept the recognized text."
                : insertion_error,
            MessageKind::Error
        );
    }

    target_ = 0;
}

void ApplicationCore::shutdown() {
    if (shutting_down_.exchange(true)) {
        return;
    }

    audio_capture_.stop();

    if (consumer_thread_.joinable()) {
        consumer_thread_.join();
    }

    asr_.close_stream();
    llm_.shutdown();
    audio_queue_.reset();
    session_active_ = false;
    finalizing_ = false;
    target_ = 0;
}

bool ApplicationCore::session_active() const {
    return session_active_;
}

bool ApplicationCore::finalizing() const {
    return finalizing_;
}
