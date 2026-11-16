#include "fakes/fake_models.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

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
  Milliseconds MaxAudio() const override { return models_.options_.max_audio; }

  void CheckGlossary(std::span<const std::string> glossary, Milliseconds longest) const override {
    models_.glossary_checks_.push_back(longest);
    std::size_t count = 0;
    for (const std::string& term : glossary) count += (count > 0 ? 2 : 0) + term.size();
    if (count > models_.options_.glossary_room) {
      throw std::invalid_argument("glossary is " + std::to_string(count) +
                                  " tokens; this model takes at most " +
                                  std::to_string(models_.options_.glossary_room));
    }
  }

  void Transcribe(const TranscribeRequest& request,
                  const WordsCallback& on_words,
                  const FractionCallback& on_progress,
                  StopToken stop) override {
    ThrowIfStopped(stop);
    ++models_.transcribe_calls_;
    if (static_cast<SampleIndex>(request.pcm.size()) > ToSamples(MaxAudio())) {
      throw std::logic_error("a request longer than MaxAudio()");
    }
    if (models_.options_.on_transcribe) models_.options_.on_transcribe(request);
    if (models_.options_.separate_alignment) {
      TranscribeText(request, on_words, on_progress, stop);
    } else {
      TranscribeTimed(request, on_words, on_progress, stop);
    }
  }

 private:
  // Words with their times, as Whisper hears them.
  void TranscribeTimed(const TranscribeRequest& request, const WordsCallback& on_words,
                       const FractionCallback& on_progress, StopToken stop) {
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
    TranscribedWords piece{.words = {}, .language = language};
    const auto hand_over = [&] {
      const SampleIndex heard = ToSamples(piece.words.back().end);
      on_words(std::move(piece));
      piece = {.words = {}, .language = language};
      on_progress(std::min(1.0, static_cast<double>(heard) / static_cast<double>(request.pcm.size())));
    };
    for (std::size_t i = 0; i < bursts.size(); ++i) {
      const Burst& burst = bursts[i];
      // Decoded times are not exact.
      const auto jitter = Milliseconds{
          (burst.id * 7919 + static_cast<int>(request.pcm.size() / 160)) % 81 -
          40};
      if (i % 5 == 0 && !piece.words.empty()) {
        hand_over();
        ThrowIfStopped(stop);
      }
      piece.words.push_back(
          {.text = WordText(burst.id, language),
           .start =
               std::max(Milliseconds{0}, ToMilliseconds(burst.begin) + jitter),
           .end = ToMilliseconds(burst.end) + jitter,
           .speaker = std::nullopt});
    }
    if (!piece.words.empty()) hand_over();
  }

  // The words of TranscribeTimed, as text alone.
  void TranscribeText(const TranscribeRequest& request, const WordsCallback& on_words,
                      const FractionCallback& on_progress, StopToken stop) {
    std::string text;
    std::string language;
    TranscribeTimed(
        request,
        [&](TranscribedWords piece) {
          language = piece.language;
          for (const Word& word : piece.words) text += (text.empty() ? "" : " ") + word.text;
        },
        [](double) {}, stop);
    if (!text.empty()) on_words({.words = {}, .language = language, .text = text});
    on_progress(1.0);
  }

  FakeModels& models_;
};

namespace {

// Times each word of the text at the burst it was heard in, in order.
class FakeAligner : public Aligner {
 public:
  explicit FakeAligner(LiveModels& live) : live_(live) {
    ++live_.aligners;
    if (live_.transcribers > 0) live_.aligner_with_transcriber = true;
  }
  ~FakeAligner() override { --live_.aligners; }

  ModelInfo Info() const override { return {"fake-aligner", "CPU", false}; }
  Milliseconds MaxAudio() const override { return Milliseconds{180000}; }

  std::vector<Word> Align(const AlignRequest& request, const FractionCallback& on_progress,
                          StopToken stop) override {
    ThrowIfStopped(stop);
    const auto bursts = FindBursts(request.pcm, ToSamples(Milliseconds{100}));
    std::vector<Word> words;
    std::size_t from = 0;
    for (std::size_t i = 0; from < request.text.size(); ++i) {
      std::size_t to = request.text.find(' ', from);
      if (to == std::string::npos) to = request.text.size();
      if (i >= bursts.size()) throw std::logic_error("more words than bursts");
      words.push_back({.text = request.text.substr(from, to - from),
                       .start = ToMilliseconds(bursts[i].begin),
                       .end = ToMilliseconds(bursts[i].end),
                       .speaker = std::nullopt});
      from = to + 1;
    }
    on_progress(1.0);
    return words;
  }

 private:
  LiveModels& live_;
};

}  // namespace

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

std::unique_ptr<Aligner> FakeModels::OpenAligner() {
  if (!options_.separate_alignment) return nullptr;
  return std::make_unique<FakeAligner>(live_);
}

std::unique_ptr<Diarizer> FakeModels::OpenDiarizer() {
  return std::make_unique<FakeDiarizer>(options_, live_);
}

}  // namespace sillok::testing
