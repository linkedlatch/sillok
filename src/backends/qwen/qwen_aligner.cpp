#include "backends/qwen/qwen_aligner.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include <mtmd.h>

#include "core/model_provider.hpp"

namespace sillok::qwen {
namespace {

// The model's timestamp classes (config.json: classify_num,
// timestamp_segment_time), which llama.cpp's converter does not record.
constexpr int kClasses = 5000;
constexpr int kSegmentMs = 80;
// Positions the model holds: 180 s of audio is 2340, and its words with their
// slots take fewer than 6000.
constexpr std::size_t kContext = 8192;
// <|audio_start|> and <|audio_end|>, which mtmd puts around the audio.
constexpr std::size_t kAudioMarkers = 2;

}  // namespace

QwenAligner::QwenAligner(const std::filesystem::path& model, const std::filesystem::path& mmproj,
                         const ComputeDevice& device, int threads)
    : model_({.text_model = model,
              .mmproj = mmproj,
              .device = device,
              .threads = threads,
              .context = static_cast<int>(kContext),
              .embeddings = false}),
      slot_(model_.Token("<timestamp>")) {
  if (model_.row_size() < kClasses) throw ModelError("not a Qwen3-ForcedAligner");
}

std::vector<int> QwenAligner::Align(std::span<const float> pcm, std::span<const AlignUnit> units,
                                    const std::function<void(double)>& on_progress,
                                    const StopToken& stop) {
  // The package's input: the audio, then each unit followed by two slots.
  std::string text;
  for (const AlignUnit& unit : units) text += unit.text + "<timestamp><timestamp>";
  // Told before the audio is encoded, instead of llama.cpp's "context full"
  // halfway.
  const std::size_t audio_tokens = AudioTokens(pcm.size());
  const std::size_t positions = audio_tokens + kAudioMarkers + model_.Tokenize(text, true).size();
  if (positions > kContext) {
    throw ModelError("too many words for the aligner: " + std::to_string(units.size()) +
                     " words and " + std::to_string(pcm.size() / kSampleRate) + " s of audio take " +
                     std::to_string(positions) + " positions, more than its " +
                     std::to_string(kContext));
  }
  const std::string prompt = mtmd_default_marker() + text;
  std::vector<int> times;
  times.reserve(units.size() * 2);
  // The output layer is the classes of 80 ms; llama.cpp puts them at the end
  // of each row, after -inf up to the vocabulary's size.
  const int first = model_.row_size() - kClasses;
  model_.Feed(
      prompt, pcm, audio_tokens, [&](llama_token token) { return token == slot_; },
      [&](llama_token, const float* row) {
        const float* classes = row + first;
        times.push_back(
            static_cast<int>(std::max_element(classes, classes + kClasses) - classes) * kSegmentMs);
      },
      false, on_progress, stop);
  if (times.size() != units.size() * 2) {
    throw ModelError("the aligner gave " + std::to_string(times.size()) + " times for " +
                     std::to_string(units.size()) + " words");
  }
  return times;
}

}  // namespace sillok::qwen

namespace sillok {
namespace {

// The package's limit (MAX_FORCE_ALIGN_INPUT_SECONDS), and shorter audio is
// padded with silence to MIN_ASR_INPUT_SECONDS.
constexpr Milliseconds kMaxAudio{180000};
constexpr std::size_t kMinSamples = kSampleRate / 2;

class WordAligner final : public Aligner {
 public:
  explicit WordAligner(const QwenAlignerConfig& config)
      : aligner_(config.model, config.mmproj, config.device, config.threads),
        info_{.name = config.name,
              .device = config.device.name,
              .fell_back_to_cpu = config.device.fell_back_to_cpu} {}

  ModelInfo Info() const override { return info_; }
  Milliseconds MaxAudio() const override { return kMaxAudio; }

  std::vector<Word> Align(const AlignRequest& request, const FractionCallback& on_progress,
                          StopToken stop) override {
    ThrowIfStopped(stop);
    const std::vector<qwen::AlignUnit> units = qwen::AlignUnits(request.text, request.language);
    if (units.empty()) {
      on_progress(1.0);
      return {};
    }
    std::span<const float> pcm = request.pcm;
    std::vector<float> padded;
    if (pcm.size() < kMinSamples) {
      padded.assign(pcm.begin(), pcm.end());
      padded.resize(kMinSamples, 0.0f);
      pcm = padded;
    }
    std::vector<Milliseconds> times;
    for (const int ms : qwen::FixTimestamps(aligner_.Align(pcm, units, on_progress, stop))) {
      times.emplace_back(ms);
    }
    on_progress(1.0);
    return qwen::TimedWords(request.text, request.language, units, times);
  }

 private:
  qwen::QwenAligner aligner_;
  ModelInfo info_;
};

}  // namespace

std::unique_ptr<Aligner> OpenQwenAligner(const QwenAlignerConfig& config) {
  return std::make_unique<WordAligner>(config);
}

}  // namespace sillok
