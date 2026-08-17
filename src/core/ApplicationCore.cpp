#include "ApplicationCore.h"

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
    std::string& error
) {
    return asr_.load(
        model_path,
        rnnt_right_context(recognition_settings_.latency),
        error
    );
}

void ApplicationCore::set_settings(
    const MarkdownCommands& commands,
    const CustomDictionarySettings& dictionary,
    const RecognitionSettings& recognition
) {
    markdown_commands_ = commands;
    custom_dictionary_ = dictionary;
    recognition_settings_ = recognition;

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

void ApplicationCore::start_session() {
    if (session_active_ || finalizing_ || shutting_down_) {
        return;
    }

    if (!asr_.loaded()) {
        platform_.show_message(
            "NeMo Talk",
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
    platform_.show_overlay(
        active_recognition_settings_.mode == RecognitionMode::Streaming
            ? "Listening..."
            : "Recording..."
    );
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

            result.text = MarkdownFormatter::format(
                transcript,
                active_markdown_commands_
            );
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

        display = MarkdownFormatter::format(
            display,
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

    result.text = MarkdownFormatter::format(
        committed,
        active_markdown_commands_
    );

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
        platform_.show_message("ASR error", result.error, MessageKind::Error);
        target_ = 0;
        return;
    }

    if (result.text.empty()) {
        target_ = 0;
        return;
    }

    if (!target_ || !platform_.is_target_active(target_)) {
        platform_.show_message(
            "NeMo Talk",
            "The active application changed while dictating, so the text was not inserted.",
            MessageKind::Warning
        );
        target_ = 0;
        return;
    }

    result.text.push_back(' ');
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
