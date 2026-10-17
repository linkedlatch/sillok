#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#include "diarization.hpp"
#include "support.hpp"
#include "whisper_engine.hpp"

using namespace sillok;
using namespace sillok::testing;

static std::vector<float> repeated(int times) {
    auto sentence = readWav(fixturePath("en-long.wav"));
    std::vector<float> samples;
    for (int i = 0; i < times; ++i) samples.insert(samples.end(), sentence.begin(), sentence.end());
    return samples;
}

static int transcribe() {
    if (!std::filesystem::exists(modelPath(testModel)) || !std::filesystem::exists(fixturePath("en-long.wav"))) {
        std::puts("skipped");
        return 0;
    }
    auto samples = repeated(6);
    static WhisperEngine engine;
    engine.load(modelPath(testModel), std::nullopt, true);
    std::thread([samples] {
        CancelToken cancel;
        try {
            engine.transcribe(samples, ChunkTranscribeOptions{"en", std::nullopt, 5}, cancel);
        } catch (...) {
        }
    }).detach();
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::puts("exiting during inference");
    std::exit(0);
}

static int diarize() {
    const auto model = modelPath("Nemotron-3-Diarization.q8_0.gguf");
    if (!std::filesystem::exists(model) || !std::filesystem::exists(fixturePath("en-long.wav"))) {
        std::puts("skipped");
        return 0;
    }
    static auto* samples = new std::vector<float>(repeated(60));
    static auto* diarizer = new SpeakerDiarizer(model, true);
    std::thread([] {
        CancelToken cancel;
        try {
            diarizer->diarize(arrayReader(*samples), samples->size() / static_cast<double>(kSampleRate), cancel);
        } catch (...) {
        }
    }).detach();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::puts("exiting during diarization");
    std::exit(0);
}

int main(int argc, char** argv) {
    return argc > 1 && std::strcmp(argv[1], "diarize") == 0 ? diarize() : transcribe();
}
