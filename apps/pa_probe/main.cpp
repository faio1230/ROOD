#include <portaudio.h>
#ifdef _WIN32
#include <pa_win_wasapi.h>
#endif

#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

struct TimingStats {
    int channels = 0;
    unsigned long callbacks = 0;
    unsigned long underflows = 0;
    unsigned long timestamp_regressions = 0;
    unsigned long first_regression_callback = 0;
    double largest_regression = 0;
    PaTime first_dac_time = 0;
    PaTime last_dac_time = 0;
    PaTime first_callback_time = 0;
    PaTime last_callback_time = 0;
    double minimum_dac_lead = std::numeric_limits<double>::infinity();
    double maximum_dac_lead = -std::numeric_limits<double>::infinity();
};

int silence_callback(const void*, void* output, unsigned long frames,
                     const PaStreamCallbackTimeInfo* time_info,
                     PaStreamCallbackFlags flags, void* user_data) {
    auto& stats = *static_cast<TimingStats*>(user_data);
    std::fill_n(static_cast<float*>(output), frames * stats.channels, 0.0f);
    if (flags & paOutputUnderflow) {
        ++stats.underflows;
    }
    if (time_info != nullptr) {
        if (stats.callbacks == 0) {
            stats.first_dac_time = time_info->outputBufferDacTime;
            stats.first_callback_time = time_info->currentTime;
        } else if (time_info->outputBufferDacTime < stats.last_dac_time) {
            ++stats.timestamp_regressions;
            if (stats.first_regression_callback == 0) {
                stats.first_regression_callback = stats.callbacks;
            }
            stats.largest_regression = std::max(
                stats.largest_regression,
                stats.last_dac_time - time_info->outputBufferDacTime);
        }
        stats.last_dac_time = time_info->outputBufferDacTime;
        stats.last_callback_time = time_info->currentTime;
        const double lead = time_info->outputBufferDacTime - time_info->currentTime;
        stats.minimum_dac_lead = std::min(stats.minimum_dac_lead, lead);
        stats.maximum_dac_lead = std::max(stats.maximum_dac_lead, lead);
    }
    ++stats.callbacks;
    return paContinue;
}

int parse_int(const char* text, int minimum, int maximum) {
    const std::string value(text);
    std::size_t consumed = 0;
    const int result = std::stoi(value, &consumed);
    if (consumed != value.size() || result < minimum || result > maximum) {
        throw std::invalid_argument("integer argument out of range");
    }
    return result;
}

void list_devices() {
    std::cout << "PortAudio " << Pa_GetVersionText() << '\n';
    const int count = Pa_GetDeviceCount();
    if (count < 0) {
        throw std::runtime_error(Pa_GetErrorText(count));
    }
    for (int index = 0; index < count; ++index) {
        const PaDeviceInfo* device = Pa_GetDeviceInfo(index);
        if (device == nullptr || device->maxOutputChannels == 0) {
            continue;
        }
        const PaHostApiInfo* api = Pa_GetHostApiInfo(device->hostApi);
        std::cout << index << " | " << (api ? api->name : "unknown")
                  << " | " << device->name
                  << " | output channels=" << device->maxOutputChannels
                  << " | default rate=" << device->defaultSampleRate << '\n';
    }
}

void probe(int index, bool exclusive, int channels, int sample_rate,
           bool run_timing, int seconds) {
    const PaDeviceInfo* device = Pa_GetDeviceInfo(index);
    if (device == nullptr || channels > device->maxOutputChannels) {
        throw std::invalid_argument("device missing or channel count exceeds PortAudio device limit");
    }
    const PaHostApiInfo* api = Pa_GetHostApiInfo(device->hostApi);
    if (api == nullptr) {
        throw std::runtime_error("host API information unavailable");
    }

    PaStreamParameters output{};
    output.device = index;
    output.channelCount = channels;
    output.sampleFormat = paFloat32;
    output.suggestedLatency = device->defaultLowOutputLatency;
#ifdef _WIN32
    PaWasapiStreamInfo wasapi{};
    if (api->type == paWASAPI) {
        wasapi.size = sizeof(wasapi);
        wasapi.hostApiType = paWASAPI;
        wasapi.version = 1;
        wasapi.flags = exclusive ? paWinWasapiExclusive : 0;
        output.hostApiSpecificStreamInfo = &wasapi;
    } else if (exclusive) {
        throw std::invalid_argument("exclusive mode is only valid for WASAPI devices");
    }
#else
    if (exclusive) {
        throw std::invalid_argument("exclusive mode is only available on Windows");
    }
#endif

    const PaError format = Pa_IsFormatSupported(nullptr, &output, sample_rate);
    std::cout << "Device " << index << " (" << api->name << ", " << device->name
              << "), " << channels << " channels, " << sample_rate << " Hz, "
              << (api->type == paWASAPI ? (exclusive ? "exclusive" : "shared") : "default") << ": "
              << (format == paFormatIsSupported ? "supported" : Pa_GetErrorText(format)) << '\n';
    if (!run_timing || format != paFormatIsSupported) {
        if (run_timing && format != paFormatIsSupported) {
            throw std::runtime_error("requested format is not supported");
        }
        return;
    }

    TimingStats stats;
    stats.channels = channels;
    PaStream* stream = nullptr;
    PaError error = Pa_OpenStream(&stream, nullptr, &output, sample_rate,
                                  paFramesPerBufferUnspecified, paNoFlag,
                                  silence_callback, &stats);
    if (error != paNoError) {
        throw std::runtime_error(std::string("Pa_OpenStream: ") + Pa_GetErrorText(error));
    }
    const PaStreamInfo* info = Pa_GetStreamInfo(stream);
    if (info != nullptr) {
        std::cout << "Opened: output latency=" << info->outputLatency
                  << " s, sample rate=" << info->sampleRate << " Hz\n";
    }
    error = Pa_StartStream(stream);
    if (error == paNoError) {
        Pa_Sleep(seconds * 1000);
        error = Pa_StopStream(stream);
    }
    const PaError close_error = Pa_CloseStream(stream);
    if (error != paNoError || close_error != paNoError) {
        throw std::runtime_error(Pa_GetErrorText(error != paNoError ? error : close_error));
    }
    std::cout << "Callbacks=" << stats.callbacks
              << ", underflows=" << stats.underflows
              << ", timestamp regressions=" << stats.timestamp_regressions << '\n';
    if (stats.timestamp_regressions != 0) {
        std::cout << "First regression at callback=" << stats.first_regression_callback
                  << ", largest backwards step=" << stats.largest_regression << " s\n";
    }
    if (stats.callbacks != 0) {
        std::cout << "Callback clock elapsed="
                  << stats.last_callback_time - stats.first_callback_time
                  << " s, DAC timestamp elapsed="
                  << stats.last_dac_time - stats.first_dac_time
                  << " s, DAC lead range=[" << stats.minimum_dac_lead
                  << ", " << stats.maximum_dac_lead << "] s\n";
    }
    std::cout << "These are PortAudio timestamps, not a measurement of physical DAC timing.\n";
}

} // namespace

int main(int argc, char* argv[]) {
    const PaError initialization = Pa_Initialize();
    if (initialization != paNoError) {
        std::cerr << "Pa_Initialize: " << Pa_GetErrorText(initialization) << '\n';
        return 1;
    }
    int result = 0;
    try {
        if (argc == 1 || (argc == 2 && std::string(argv[1]) == "--list")) {
            list_devices();
        } else if ((argc == 5 || argc == 6) && std::string(argv[1]) == "--format") {
            const std::string mode(argv[3]);
            if (mode != "shared" && mode != "exclusive" && mode != "default") {
                throw std::invalid_argument("mode must be shared, exclusive or default");
            }
            probe(parse_int(argv[2], 0, 10000), mode == "exclusive",
                  parse_int(argv[4], 1, 256),
                  argc == 6 ? parse_int(argv[5], 8000, 384000) : 48000,
                  false, 0);
        } else if ((argc == 6 || argc == 7) && std::string(argv[1]) == "--timing") {
            const std::string mode(argv[3]);
            if (mode != "shared" && mode != "exclusive" && mode != "default") {
                throw std::invalid_argument("mode must be shared, exclusive or default");
            }
            probe(parse_int(argv[2], 0, 10000), mode == "exclusive",
                  parse_int(argv[5], 1, 256),
                  argc == 7 ? parse_int(argv[6], 8000, 384000) : 48000,
                  true,
                  parse_int(argv[4], 1, 3600));
        } else {
            throw std::invalid_argument(
                "usage: rood_pa_probe [--list | --format INDEX shared|exclusive|default CHANNELS [RATE] | "
                "--timing INDEX shared|exclusive|default SECONDS CHANNELS [RATE]]");
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    const PaError termination = Pa_Terminate();
    if (termination != paNoError) {
        std::cerr << "Pa_Terminate: " << Pa_GetErrorText(termination) << '\n';
        result = 1;
    }
    return result;
}
