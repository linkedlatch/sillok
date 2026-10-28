#include "fakes/fake_models.hpp"

#include <algorithm>
#include <cmath>

#include "core/cancel.hpp"

namespace sillok::testing {
namespace {

// Loud enough that NormalizeLevel() never raises a burst, even one word
// alone in two seconds of silence: amplitude carries the word id.
constexpr float kBaseAmplitude = 0.2f;
constexpr float kAmplitudeStep = 0.0005f;
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
    float peak = 0.0f;
    while (i < n && std::abs(pcm[static_cast<std::size_t>(i)]) > kSilence) {
      peak = std::max(peak, std::abs(pcm[static_cast<std::size_t>(i)]));
      ++i;
    }
    if (i - begin < min_samples) continue;
    const int id = static_cast<int>(
        std::lround((peak - kBaseAmplitude) / kAmplitudeStep));
    bursts.push_back({begin, i, id});
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
  TranscriberCapabilities Capabilities() const override {
    return {.max_audio = models_.options_.max_audio,
            .detects_language = models_.options_.detects_language,
            .max_prompt_tokens = 20};
  }

  std::vector<LanguageScore> DetectLanguage(
      std::span<const float> pcm, std::span<const std::string> candidates,
      std::stop_token stop) override {
    ThrowIfStopped(stop);
    std::map<std::string, double> time;
    double total = 0.0;
    for (const Burst& burst : FindBursts(pcm, ToSamples(Millis{100}))) {
      const auto length = static_cast<double>(burst.end - burst.begin);
      time[LanguageOf(models_.options_, burst.id)] += length;
      total += length;
    }
    std::vector<LanguageScore> scores;
    for (const std::string& language : candidates) {
      const double share = total > 0.0 ? time[language] / total : 0.5;
      scores.push_back({language, static_cast<float>(std::clamp(share, 0.02, 0.98))});
    }
    return scores;
  }

  int CountTokens(std::string_view text) const override {
    int words = 0;
    bool in_word = false;
    for (const char c : text) {
      if (c != ' ' && !in_word) ++words;
      in_word = c != ' ';
    }
    return words;
  }

  std::vector<TokenSegment> Transcribe(const TranscribeRequest& request,
                                       std::stop_token stop) override {
    ThrowIfStopped(stop);
    ++models_.transcribe_calls_;
    if (models_.options_.on_transcribe) models_.options_.on_transcribe(request);
    ThrowIfStopped(stop);

    std::vector<TokenSegment> segments;
    const auto bursts = FindBursts(request.pcm, ToSamples(Millis{100}));
    for (std::size_t i = 0; i < bursts.size(); ++i) {
      const Burst& burst = bursts[i];
      // The same word gets slightly different times in different windows.
      const auto jitter = Millis{
          (burst.id * 7919 + static_cast<int>(request.pcm.size() / 160)) % 81 -
          40};
      const bool right_language =
          LanguageOf(models_.options_, burst.id) == request.language;
      if (i % 5 == 0) segments.emplace_back();
      segments.back().tokens.push_back(
          {.text = " " + WordText(burst.id, request.language),
           .start = std::max(Millis{0}, ToMillis(burst.begin) + jitter),
           .end = ToMillis(burst.end) + jitter,
           .probability = right_language ? 0.9f : 0.05f});
    }
    return segments;
  }

 private:
  FakeModels& models_;
};

AudioBuffer RenderScript(const std::vector<ScriptWord>& words,
                         Millis duration) {
  std::vector<float> pcm(static_cast<std::size_t>(ToSamples(duration)), 0.0f);
  for (const ScriptWord& word : words) {
    const float amplitude =
        kBaseAmplitude + kAmplitudeStep * static_cast<float>(word.id);
    for (SampleIndex i = ToSamples(word.start); i < ToSamples(word.end); ++i) {
      pcm[static_cast<std::size_t>(i)] = (i / 20) % 2 == 0 ? amplitude : -amplitude;
    }
  }
  AudioBuffer audio;
  audio.Push(pcm, kSampleRate, 1);
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
