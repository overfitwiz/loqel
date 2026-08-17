#include "AudioCapture.h"

#include "AudioQueue.h"

#include <audioclient.h>
#include <ks.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdint>
#include <cwchar>
#include <exception>
#include <string>
#include <utility>
#include <vector>

#include "windows/WindowsText.h"

using Microsoft::WRL::ComPtr;

namespace {

std::wstring format_hresult(
    const wchar_t* operation,
    HRESULT result
) {
    wchar_t code[24] = {};

    swprintf_s(
        code,
        L"0x%08lX",
        static_cast<unsigned long>(result)
    );

    return
        std::wstring(operation) +
        L" failed (" +
        code +
        L")";
}

class WasapiDevice {
public:
    ~WasapiDevice() {
        if (started_ && client_) {
            client_->Stop();
        }

        if (event_) {
            CloseHandle(event_);
        }

        if (format_) {
            CoTaskMemFree(format_);
        }
    }

    bool open(std::wstring& error) {
        HRESULT result =
            CoCreateInstance(
                __uuidof(MMDeviceEnumerator),
                nullptr,
                CLSCTX_ALL,
                IID_PPV_ARGS(&enumerator_)
            );

        if (FAILED(result)) {
            error = format_hresult(
                L"Creating audio device enumerator",
                result
            );

            return false;
        }

        result =
            enumerator_->GetDefaultAudioEndpoint(
                eCapture,
                eCommunications,
                &device_
            );

        if (FAILED(result)) {
            result =
                enumerator_->GetDefaultAudioEndpoint(
                    eCapture,
                    eConsole,
                    &device_
                );
        }

        if (FAILED(result)) {
            error = format_hresult(
                L"Opening default microphone",
                result
            );

            return false;
        }

        result =
            device_->Activate(
                __uuidof(IAudioClient),
                CLSCTX_ALL,
                nullptr,
                &client_
            );

        if (FAILED(result)) {
            error = format_hresult(
                L"Activating microphone",
                result
            );

            return false;
        }

        result =
            client_->GetMixFormat(
                &format_
            );

        if (FAILED(result) || !format_) {
            error = format_hresult(
                L"Reading microphone format",
                result
            );

            return false;
        }

        if (!classify_format(error)) {
            return false;
        }

        event_ =
            CreateEventW(
                nullptr,
                FALSE,
                FALSE,
                nullptr
            );

        if (!event_) {
            error =
                L"Could not create microphone event.";

            return false;
        }

        const DWORD flags =
            AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
            AUDCLNT_STREAMFLAGS_NOPERSIST;

        result =
            client_->Initialize(
                AUDCLNT_SHAREMODE_SHARED,
                flags,
                0,
                0,
                format_,
                nullptr
            );

        if (FAILED(result)) {
            error = format_hresult(
                L"Initializing microphone",
                result
            );

            return false;
        }

        result =
            client_->SetEventHandle(
                event_
            );

        if (FAILED(result)) {
            error = format_hresult(
                L"Setting microphone event",
                result
            );

            return false;
        }

        result =
            client_->GetService(
                IID_PPV_ARGS(&capture_)
            );

        if (FAILED(result)) {
            error = format_hresult(
                L"Creating capture client",
                result
            );

            return false;
        }

        result = client_->Start();

        if (FAILED(result)) {
            error = format_hresult(
                L"Starting microphone",
                result
            );

            return false;
        }

        started_ = true;

        return true;
    }

    HANDLE event_handle() const {
        return event_;
    }

    int sample_rate() const {
        return static_cast<int>(
            format_->nSamplesPerSec
        );
    }

    bool drain(
        AudioQueue& queue,
        std::wstring& error
    ) {
        UINT32 packet_frames = 0;

        HRESULT result =
            capture_->GetNextPacketSize(
                &packet_frames
            );

        if (FAILED(result)) {
            error = format_hresult(
                L"Reading packet size",
                result
            );

            return false;
        }

        while (packet_frames > 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;

            result =
                capture_->GetBuffer(
                    &data,
                    &frames,
                    &flags,
                    nullptr,
                    nullptr
                );

            if (FAILED(result)) {
                error = format_hresult(
                    L"Reading microphone audio",
                    result
                );

                return false;
            }

            std::vector<float> mono(
                frames,
                0.0f
            );

            if (
                !(flags & AUDCLNT_BUFFERFLAGS_SILENT) &&
                data
            ) {
                convert_to_mono(
                    data,
                    frames,
                    mono
                );
            }

            if (
                flags &
                AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY
            ) {
                OutputDebugStringW(
                    L"*** AUDIO DISCONTINUITY ***\n"
                );
            }

            // Important:
            // release WASAPI's buffer before touching
            // our queue / consumer.
            result =
                capture_->ReleaseBuffer(
                    frames
                );

            if (FAILED(result)) {
                error = format_hresult(
                    L"Releasing microphone buffer",
                    result
                );

                return false;
            }

            if (!queue.push(std::move(mono))) {
                return false;
            }

            result =
                capture_->GetNextPacketSize(
                    &packet_frames
                );

            if (FAILED(result)) {
                error = format_hresult(
                    L"Reading packet size",
                    result
                );

                return false;
            }
        }

        return true;
    }

private:
    enum class SampleKind {
        Float32,
        Pcm16,
        Pcm24,
        Pcm32
    };

    bool classify_format(
        std::wstring& error
    ) {
        WORD tag =
            format_->wFormatTag;

        if (
            tag == WAVE_FORMAT_EXTENSIBLE &&
            format_->cbSize >= 22
        ) {
            const auto* extended =
                reinterpret_cast<
                    const WAVEFORMATEXTENSIBLE*
                >(format_);

            if (
                extended->SubFormat ==
                KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
            ) {
                tag = WAVE_FORMAT_IEEE_FLOAT;
            }
            else if (
                extended->SubFormat ==
                KSDATAFORMAT_SUBTYPE_PCM
            ) {
                tag = WAVE_FORMAT_PCM;
            }
        }

        if (
            tag == WAVE_FORMAT_IEEE_FLOAT &&
            format_->wBitsPerSample == 32
        ) {
            sample_kind_ =
                SampleKind::Float32;
        }
        else if (
            tag == WAVE_FORMAT_PCM &&
            format_->wBitsPerSample == 16
        ) {
            sample_kind_ =
                SampleKind::Pcm16;
        }
        else if (
            tag == WAVE_FORMAT_PCM &&
            format_->wBitsPerSample == 24
        ) {
            sample_kind_ =
                SampleKind::Pcm24;
        }
        else if (
            tag == WAVE_FORMAT_PCM &&
            format_->wBitsPerSample == 32
        ) {
            sample_kind_ =
                SampleKind::Pcm32;
        }
        else {
            error =
                L"Unsupported microphone sample format.";

            return false;
        }

        if (
            format_->nChannels == 0 ||
            format_->nSamplesPerSec < 8000 ||
            format_->nSamplesPerSec > 96000
        ) {
            error =
                L"Microphone format is outside "
                L"the supported range.";

            return false;
        }

        return true;
    }

    float read_sample(
        const BYTE* sample
    ) const {
        switch (sample_kind_) {
            case SampleKind::Float32:
                return
                    *reinterpret_cast<
                        const float*
                    >(sample);

            case SampleKind::Pcm16:
                return
                    static_cast<float>(
                        *reinterpret_cast<
                            const int16_t*
                        >(sample)
                    ) /
                    32768.0f;

            case SampleKind::Pcm24: {
                int32_t value =
                    static_cast<int32_t>(sample[0]) |
                    (
                        static_cast<int32_t>(sample[1])
                        << 8
                    ) |
                    (
                        static_cast<int32_t>(sample[2])
                        << 16
                    );

                if (value & 0x00800000) {
                    value |=
                        static_cast<int32_t>(
                            0xFF000000
                        );
                }

                return
                    static_cast<float>(value) /
                    8388608.0f;
            }

            case SampleKind::Pcm32:
                return
                    static_cast<float>(
                        *reinterpret_cast<
                            const int32_t*
                        >(sample)
                    ) /
                    2147483648.0f;
        }

        return 0.0f;
    }

    void convert_to_mono(
        const BYTE* data,
        UINT32 frames,
        std::vector<float>& mono
    ) {
        const UINT32 channels =
            format_->nChannels;

        const UINT32 bytes_per_sample =
            format_->wBitsPerSample / 8;

        for (
            UINT32 frame = 0;
            frame < frames;
            ++frame
        ) {
            const BYTE* frame_data =
                data +
                static_cast<std::size_t>(frame) *
                format_->nBlockAlign;

            float sum = 0.0f;

            for (
                UINT32 channel = 0;
                channel < channels;
                ++channel
            ) {
                sum += read_sample(
                    frame_data +
                    static_cast<std::size_t>(channel) *
                    bytes_per_sample
                );
            }

            mono[frame] =
                std::clamp(
                    sum /
                        static_cast<float>(channels),
                    -1.0f,
                    1.0f
                );
        }
    }

    ComPtr<IMMDeviceEnumerator> enumerator_;
    ComPtr<IMMDevice> device_;

    ComPtr<IAudioClient> client_;
    ComPtr<IAudioCaptureClient> capture_;

    HANDLE event_ = nullptr;

    WAVEFORMATEX* format_ = nullptr;

    SampleKind sample_kind_ =
        SampleKind::Float32;

    bool started_ = false;
};

}

WindowsAudioCapture::~WindowsAudioCapture() {
    stop();
}

bool WindowsAudioCapture::start(
    AudioQueue& queue
) {
    if (thread_.joinable()) {
        return false;
    }

    stop_event_ =
        CreateEventW(
            nullptr,
            TRUE,
            FALSE,
            nullptr
        );

    if (!stop_event_) {
        queue.fail(
            "Could not create audio stop event."
        );

        return false;
    }

    try {
        thread_ =
            std::thread(
                &WindowsAudioCapture::capture_loop,
                this,
                &queue
            );
    }
    catch (...) {
        CloseHandle(stop_event_);
        stop_event_ = nullptr;

        queue.fail(
            "Could not start microphone thread."
        );

        return false;
    }

    return true;
}

void WindowsAudioCapture::stop() {
    if (stop_event_) {
        SetEvent(stop_event_);
    }

    if (thread_.joinable()) {
        thread_.join();
    }

    if (stop_event_) {
        CloseHandle(stop_event_);
        stop_event_ = nullptr;
    }
}

void WindowsAudioCapture::capture_loop(
    AudioQueue* queue
) {
    const HRESULT com_result =
        CoInitializeEx(
            nullptr,
            COINIT_MULTITHREADED
        );

    const bool should_uninitialize =
        SUCCEEDED(com_result);

    if (
        FAILED(com_result) &&
        com_result != RPC_E_CHANGED_MODE
    ) {
        queue->fail(
            WindowsText::to_utf8(format_hresult(
                L"Initializing COM",
                com_result
            ))
        );

        return;
    }

    try {
        WasapiDevice microphone;

        std::wstring error;

        if (!microphone.open(error)) {
            queue->fail(
                WindowsText::to_utf8(error)
            );

            if (should_uninitialize) {
                CoUninitialize();
            }

            return;
        }

        queue->start(
            microphone.sample_rate()
        );

        HANDLE events[] = {
            stop_event_,
            microphone.event_handle()
        };

        bool ok = true;

        while (ok) {
            const DWORD wait_result =
                WaitForMultipleObjects(
                    2,
                    events,
                    FALSE,
                    INFINITE
                );

            if (wait_result == WAIT_OBJECT_0) {
                break;
            }

            if (
                wait_result !=
                WAIT_OBJECT_0 + 1
            ) {
                error =
                    L"Waiting for microphone failed.";

                ok = false;

                break;
            }

            ok =
                microphone.drain(
                    *queue,
                    error
                );
        }

        // Capture anything already waiting when F8
        // was released.
        if (ok) {
            ok =
                microphone.drain(
                    *queue,
                    error
                );
        }

        if (!ok && !error.empty()) {
            queue->fail(
                WindowsText::to_utf8(error)
            );
        }
        else {
            queue->close();
        }
    }
    catch (const std::exception& exception) {
        queue->fail("Microphone capture failed.");
    }
    catch (...) {
        queue->fail(
            "Unknown microphone capture error."
        );
    }

    if (should_uninitialize) {
        CoUninitialize();
    }
}
