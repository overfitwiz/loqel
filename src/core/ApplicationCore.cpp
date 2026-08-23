#include "ApplicationCore.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <sstream>
#include <system_error>
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
    const CleanupSettings& cleanup
) {
    if (output_mode == OutputMode::Plain) {
        return transcript;
    }

    return remove_cleanup_words(transcript, cleanup);
}

void preserve_first_error(std::string& destination, std::string error) {
    if (destination.empty()) {
        destination = std::move(error);
    }
}

bool create_debug_session_directory(
    const DebugSettings& settings,
    std::filesystem::path& directory,
    std::string& error
) {
    if (settings.output_directory.empty()) {
        error = "The debug output directory is not configured.";
        return false;
    }

    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm local_time = {};

#ifdef _WIN32
    localtime_s(&local_time, &time);
#else
    localtime_r(&time, &local_time);
#endif

    const auto milliseconds = std::chrono::duration_cast<
        std::chrono::milliseconds
    >(now.time_since_epoch()).count() % 1000;

    std::ostringstream name;
    name << std::put_time(&local_time, "%Y%m%d-%H%M%S")
         << '-' << std::setfill('0') << std::setw(3) << milliseconds;

    std::error_code filesystem_error;
    std::filesystem::create_directories(
        settings.output_directory,
        filesystem_error
    );

    if (filesystem_error) {
        error = "Could not create the debug output directory.";
        return false;
    }

    for (int suffix = 0; suffix < 1000; ++suffix) {
        std::string candidate_name = name.str();
        if (suffix > 0) {
            candidate_name += '-' + std::to_string(suffix);
        }

        const std::filesystem::path candidate =
            settings.output_directory / candidate_name;

        filesystem_error.clear();
        if (std::filesystem::create_directory(candidate, filesystem_error)) {
            directory = candidate;
            return true;
        }

        if (filesystem_error) {
            error = "Could not create the debug session directory.";
            return false;
        }
    }

    error = "Could not allocate a unique debug session directory.";
    return false;
}

void write_u16(std::ofstream& output, std::uint16_t value) {
    const char bytes[] = {
        static_cast<char>(value & 0xff),
        static_cast<char>((value >> 8) & 0xff)
    };
    output.write(bytes, sizeof(bytes));
}

void write_u32(std::ofstream& output, std::uint32_t value) {
    const char bytes[] = {
        static_cast<char>(value & 0xff),
        static_cast<char>((value >> 8) & 0xff),
        static_cast<char>((value >> 16) & 0xff),
        static_cast<char>((value >> 24) & 0xff)
    };
    output.write(bytes, sizeof(bytes));
}

bool write_debug_audio(
    const std::filesystem::path& path,
    const std::vector<float>& samples,
    int sample_rate,
    std::string& error
) {
    constexpr std::uint32_t kHeaderBytes = 36;
    constexpr std::uint16_t kChannels = 1;
    constexpr std::uint16_t kBitsPerSample = 16;

    if (sample_rate <= 0) {
        error = "Could not save debug audio: invalid sample rate.";
        return false;
    }

    if (samples.size() > std::numeric_limits<std::uint32_t>::max() / 2) {
        error = "Could not save debug audio: recording is too large.";
        return false;
    }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "Could not open the debug audio file.";
        return false;
    }

    const auto data_bytes = static_cast<std::uint32_t>(samples.size() * 2);
    output.write("RIFF", 4);
    write_u32(output, kHeaderBytes + data_bytes);
    output.write("WAVEfmt ", 8);
    write_u32(output, 16);
    write_u16(output, 1);
    write_u16(output, kChannels);
    write_u32(output, static_cast<std::uint32_t>(sample_rate));
    write_u32(output, static_cast<std::uint32_t>(sample_rate * 2));
    write_u16(output, 2);
    write_u16(output, kBitsPerSample);
    output.write("data", 4);
    write_u32(output, data_bytes);

    for (float sample : samples) {
        const float clamped = std::max(-1.0f, std::min(1.0f, sample));
        const auto pcm = static_cast<std::int16_t>(
            clamped < 0.0f ? clamped * 32768.0f : clamped * 32767.0f
        );
        write_u16(output, static_cast<std::uint16_t>(pcm));
    }

    if (!output) {
        error = "Could not write the debug audio file.";
        return false;
    }

    return true;
}

bool write_debug_text(
    const std::filesystem::path& path,
    const std::string& text,
    std::string& error
) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "Could not open a debug transcript file.";
        return false;
    }

    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.put('\n');

    if (!output) {
        error = "Could not write a debug transcript file.";
        return false;
    }

    return true;
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
    const CustomDictionarySettings& dictionary,
    const RecognitionSettings& recognition,
    const CleanupSettings& cleanup,
    const LlmSettings& llm,
    const DebugSettings& debug
) {
    custom_dictionary_ = dictionary;
    recognition_settings_ = recognition;
    cleanup_settings_ = cleanup;
    llm_settings_ = llm;
    debug_settings_ = debug;

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
    active_custom_dictionary_ = custom_dictionary_;
    active_recognition_settings_ = recognition_settings_;
    active_cleanup_settings_ = cleanup_settings_;
    active_debug_settings_ = debug_settings_;
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

    std::filesystem::path debug_directory;
    if (active_debug_settings_.enabled) {
        create_debug_session_directory(
            active_debug_settings_,
            debug_directory,
            result.debug_error
        );
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

        if (!debug_directory.empty()) {
            std::string debug_error;
            if (!write_debug_audio(
                    debug_directory / "audio.wav",
                    recording,
                    sample_rate,
                    debug_error
                )) {
                preserve_first_error(
                    result.debug_error,
                    std::move(debug_error)
                );
            }
        }

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

            if (!debug_directory.empty()) {
                std::string debug_error;
                if (!write_debug_text(
                        debug_directory / "transcript.txt",
                        transcript,
                        debug_error
                    )) {
                    preserve_first_error(
                        result.debug_error,
                        std::move(debug_error)
                    );
                }
            }

            result.text = prepare_output(
                transcript,
                active_output_mode_,
                active_cleanup_settings_
            );

            if (
                !llm_.process(result.text, result.text, result.error) &&
                result.error.empty()
            ) {
                result.error = "LLM text correction failed.";
            }

            if (!debug_directory.empty()) {
                std::string debug_error;
                if (!write_debug_text(
                        debug_directory / "llm-correction.txt",
                        result.text,
                        debug_error
                    )) {
                    preserve_first_error(
                        result.debug_error,
                        std::move(debug_error)
                    );
                }
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
            active_cleanup_settings_
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
    std::vector<float> debug_recording;

    while (queue->pop(samples)) {
        if (!debug_directory.empty()) {
            debug_recording.insert(
                debug_recording.end(),
                samples.begin(),
                samples.end()
            );
        }

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

    if (!debug_directory.empty()) {
        std::string debug_error;
        if (!write_debug_audio(
                debug_directory / "audio.wav",
                debug_recording,
                sample_rate,
                debug_error
            )) {
            preserve_first_error(
                result.debug_error,
                std::move(debug_error)
            );
        }

        debug_error.clear();
        if (!write_debug_text(
                debug_directory / "transcript.txt",
                committed,
                debug_error
            )) {
            preserve_first_error(
                result.debug_error,
                std::move(debug_error)
            );
        }
    }

    result.text = prepare_output(
        committed,
        active_output_mode_,
        active_cleanup_settings_
    );

    if (
        result.error.empty() &&
        !llm_.process(result.text, result.text, result.error) &&
        result.error.empty()
    ) {
        result.error = "LLM text correction failed.";
    }

    if (!debug_directory.empty()) {
        std::string debug_error;
        if (!write_debug_text(
                debug_directory / "llm-correction.txt",
                result.text,
                debug_error
            )) {
            preserve_first_error(
                result.debug_error,
                std::move(debug_error)
            );
        }
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

    const auto report_debug_error = [&] {
        if (!result.debug_error.empty()) {
            platform_.show_message(
                "Debug mode",
                result.debug_error,
                MessageKind::Warning
            );
        }
    };

    if (!result.error.empty()) {
        platform_.show_message(
            "Dictation error",
            result.error,
            MessageKind::Error
        );
        target_ = 0;
        report_debug_error();
        return;
    }

    if (result.text.empty()) {
        target_ = 0;
        report_debug_error();
        return;
    }

    if (!target_ || !platform_.is_target_active(target_)) {
        platform_.show_message(
            "loqel",
            "The active application changed while dictating, so the text was not inserted.",
            MessageKind::Warning
        );
        target_ = 0;
        report_debug_error();
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
    report_debug_error();
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
