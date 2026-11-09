#include "fakes/fake_models.hpp"

#include <algorithm>
#include <cmath>

#include "core/cancel.hpp"

namespace sillok::testing {
namespace {

// The length of a half wave carries the word id, since the input evens out
// amplitudes.
constexpr float kAmplitude = 0.2f;
constexpr int kShortestHalfWave = 4;
constexpr float kSilence = 0.05f;

struct Burst {
  SampleIndex begin = 0;
  SampleIndex end = 0;
  int id = 0;
};

// Word bursts in `pcm`, ignoring fragments shorter than `min_samples`.
std::vector<Burst> FindBursts(std::span<const float> pcm,
                              SampleIndex min_samples) {
  std::vector<Burst> bursts;
  SampleIndex i = 0;
  const auto n = static_cast<SampleIndex>(pcm.size());
  while (i < n) {
    if (std::abs(pcm[static_cast<std::size_t>(i)]) <= kSilence) {
      ++i;
      continue;
    }
    const SampleIndex begin = i;
    SampleIndex half_wave = 0;  // the longest: the first and last may be cut
    SampleIndex run = 0;
    while (i < n && std::abs(pcm[static_cast<std::size_t>(i)]) > kSilence) {
      const bool same_sign = i > begin && (pcm[static_cast<std::size_t>(i)] > 0.0f) ==
                                              (pcm[static_cast<std::size_t>(i - 1)] > 0.0f);
      run = same_sign ? run + 1 : 1;
      half_wave = std::max(half_wave, run);
      ++i;
    }
    if (i - begin < min_samples) continue;
    bursts.push_back({begin, i, static_cast<int>(half_wave) - kShortestHalfWave});
  }
  return bursts;
}

std::string LanguageOf(const FakeOptions& options, int id) {
  const auto it = options.languages.find(id);
  return it == options.languages.end() ? "en" : it->second;
}

class FakeVoiceDetector : public VoiceDetector {
 public:
  explicit FakeVoiceDetector(LiveModels& live) : live_(live) { ++live_.detectors; }
  ~FakeVoiceDetector() override { --live_.detectors; }

  ModelInfo Info() const override { return {"fake-vad", "CPU", false}; }
  int FrameSamples() const override { return 512; }
  std::vector<float> Detect(std::span<const float> pcm) override {
    std::vector<float> probabilities;
    for (std::size_t begin = 0; begin < pcm.size(); begin += 512) {
      float peak = 0.0f;
      for (std::size_t i = begin; i < std::min(begin + 512, pcm.size()); ++i) {
        peak = std::max(peak, std::abs(pcm[i]));
      }
      probabilities.push_back(peak > kSilence ? 1.0f : 0.0f);
    }
    return probabilities;
  }

 private:
  LiveModels& live_;
};

class FakeDiarizer : public Diarizer {
 public:
  FakeDiarizer(const FakeOptions& options, LiveModels& live)
      : options_(options), live_(live) {
    ++live_.diarizers;
    if (live_.transcribers > 0) live_.transcriber_with_diarizer = true;
  }
  ~FakeDiarizer() override { --live_.diarizers; }

  ModelInfo Info() const override { return {"fake-diarizer", "CPU", false}; }
  void Push(std::span<const float> pcm) override {
    audio_.insert(audio_.end(), pcm.begin(), pcm.end());
  }
  std::vector<SpeakerTurn> Finish() override {
    std::vector<SpeakerTurn> turns;
    for (const Burst& burst : FindBursts(audio_, 1)) {
      const auto it = options_.speakers.find(burst.id);
      const int speaker = it == options_.speakers.end() ? 0 : it->second;
      if (!turns.empty() && turns.back().speaker == speaker &&
          burst.begin - turns.back().range.end < kSampleRate) {
        turns.back().range.end = burst.end;
      } else {
        turns.push_back({{burst.begin, burst.end}, speaker});
      }
    }
    return turns;
  }

 private:
  const FakeOptions& options_;
  LiveModels& live_;
  std::vector<float> audio_;
};

}  // namespace

class FakeTranscriber : public Transcriber {
 public:
  explicit FakeTranscriber(FakeModels& models) : models_(models) {
    ++models_.live_.transcribers;
    if (models_.live_.diarizers > 0) {
      models_.live_.transcriber_with_diarizer = true;
    }
  }
  ~FakeTranscriber() override { --models_.live_.transcribers; }

  ModelInfo Info() const override { return {"fake-transcriber", "CPU", false}; }

  void Transcribe(const TranscribeRequest& request,
                  const TokenSegmentCallback& on_segment,
                  StopToken stop) override {
    ThrowIfStopped(stop);
    ++models_.transcribe_calls_;
    if (models_.options_.on_transcribe) models_.options_.on_transcribe(request);
    ThrowIfStopped(stop);

    const auto bursts = FindBursts(request.pcm, ToSamples(Milliseconds{100}));
    // Without a language, the one spoken longest in the first 30 s, as
    // Whisper hears it.
    std::string language = request.language;
    if (language.empty()) {
      std::map<std::string, SampleIndex> time;
      for (const Burst& burst : bursts) {
        if (burst.begin >= ToSamples(Milliseconds{30000})) break;
        time[LanguageOf(models_.options_, burst.id)] += burst.end - burst.begin;
      }
      language = time.empty() ? "en"
                              : std::max_element(time.begin(), time.end(),
                                                 [](const auto& a, const auto& b) {
                                                   return a.second < b.second;
                                                 })->first;
    }
    TokenSegment segment{.tokens = {}, .language = language};
    for (std::size_t i = 0; i < bursts.size(); ++i) {
      const Burst& burst = bursts[i];
      // Decoded times are not exact.
      const auto jitter = Milliseconds{
          (burst.id * 7919 + static_cast<int>(request.pcm.size() / 160)) % 81 -
          40};
      if (i % 5 == 0 && !segment.tokens.empty()) {
        on_segment(segment);
        segment.tokens.clear();
        ThrowIfStopped(stop);
      }
      segment.tokens.push_back(
          {.text = " " + WordText(burst.id, language),
           .start =
               std::max(Milliseconds{0}, ToMilliseconds(burst.begin) + jitter),
           .end = ToMilliseconds(burst.end) + jitter});
    }
    if (!segment.tokens.empty()) on_segment(segment);
  }

 private:
  FakeModels& models_;
};

AudioBuffer RenderScript(const std::vector<ScriptWord>& words,
                         Milliseconds duration) {
  std::vector<float> pcm(static_cast<std::size_t>(ToSamples(duration)), 0.0f);
  for (const ScriptWord& word : words) {
    const SampleIndex half_wave = kShortestHalfWave + word.id;
    for (SampleIndex i = ToSamples(word.start); i < ToSamples(word.end); ++i) {
      pcm[static_cast<std::size_t>(i)] = (i / half_wave) % 2 == 0 ? kAmplitude : -kAmplitude;
    }
  }
  AudioBuffer audio;
  audio.Push(pcm);
  audio.Finish();
  return audio;
}

std::string WordText(int id, const std::string& language) {
  return language + std::to_string(id);
}

std::unique_ptr<VoiceDetector> FakeModels::OpenVoiceDetector() {
  return std::make_unique<FakeVoiceDetector>(live_);
}

std::unique_ptr<Transcriber> FakeModels::OpenTranscriber() {
  return std::make_unique<FakeTranscriber>(*this);
}

std::unique_ptr<Diarizer> FakeModels::OpenDiarizer() {
  return std::make_unique<FakeDiarizer>(options_, live_);
}

}  // namespace sillok::testing
