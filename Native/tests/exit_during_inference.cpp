#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include "support.hpp"
#include "whisper_engine.hpp"

using namespace sillok;
using namespace sillok::testing;

int main() {
    if (!std::filesystem::exists(modelPath(testModel)) || !std::filesystem::exists(fixturePath("en-long.wav"))) {
        std::puts("skipped");
        return 0;
    }
    auto sentence = readWav(fixturePath("en-long.wav"));
    std::vector<float> samples;
    for (int i = 0; i < 6; ++i) samples.insert(samples.end(), sentence.begin(), sentence.end());

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
