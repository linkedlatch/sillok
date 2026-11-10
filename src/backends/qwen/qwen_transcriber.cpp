#include "backends/qwen/qwen_transcriber.hpp"

#include <algorithm>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

#include <mtmd.h>

#include "backends/qwen/llama_audio.hpp"
#include "backends/qwen/qwen_aligner.hpp"
#include "backends/qwen/qwen_text.hpp"
#include "core/model_provider.hpp"
#include "core/postprocess.hpp"

namespace sillok {
namespace {

using qwen::LlamaAudio;

// Shares of a request's progress: encoding the audio, writing the text
// (estimated from its length), aligning.
constexpr double kEncodeShare = 0.1;
constexpr double kAlignFrom = 0.85;
// Expected tokens a second of speech, to estimate how far the text is.
constexpr double kTokensPerSecond = 5.0;
// Text tokens at most: far more than any speech gives, a stop for a model
// caught in a loop.
std::size_t MaxTokens(std::size_t samples) { return 64 + 16 * samples / kSampleRate; }

// Whether `tokens` end in a pattern of up to 20 tokens repeated 20 times:
// the model is caught in a loop (FixRepetitions then keeps it once).
bool EndsInLoop(std::span<const llama_token> tokens) {
  constexpr std::size_t kRepeats = 20;
  for (std::size_t k = 1; k <= 20; ++k) {
    if (tokens.size() < k * kRepeats) break;
    const std::size_t from = tokens.size() - k * kRepeats;
    bool loop = true;
    for (std::size_t i = from + k; i < tokens.size() && loop; ++i) loop = tokens[i] == tokens[i - k];
    if (loop) return true;
  }
  return false;
}

std::size_t ArgMax(const float* row, std::size_t size) {
  return static_cast<std::size_t>(std::max_element(row, row + size) - row);
}

class QwenTranscriber final : public Transcriber {
 public:
  explicit QwenTranscriber(const QwenConfig& config)
      : asr_({.text_model = config.asr_model,
              .mmproj = config.asr_mmproj,
              .device = config.device,
              .threads = config.threads,
              .context = 8192,
              .embeddings = false}),
        aligner_(config.aligner_model, config.aligner_mmproj, config.device, config.threads),
        info_{.name = config.name,
              .device = config.device.name,
              .fell_back_to_cpu = config.device.fell_back_to_cpu} {
    // What may follow the prompt when the model tells the language: one of
    // these, "None" for no speech.
    for (const std::string& code : config.languages) {
      const std::string_view name = qwen::LanguageName(code);
      if (name.empty()) throw ModelError("Qwen3-ASR does not know the language " + code);
      language_starts_.push_back(asr_.Tokenize("language " + std::string(name) + "<asr_text>", true));
    }
    language_starts_.push_back(asr_.Tokenize("language None<asr_text>", true));
  }

  ModelInfo Info() const override { return info_; }

  void Transcribe(const TranscribeRequest& request, const WordsCallback& on_words,
                  const FractionCallback& on_progress, StopToken stop) override {
    ThrowIfStopped(stop);
    const qwen::AsrOutput heard = Listen(request, on_progress, stop);
    if (heard.language.empty() || heard.text.empty()) {
      on_progress(1.0);
      return;
    }
    const std::vector<qwen::AlignUnit> units = qwen::AlignUnits(heard.text, heard.language);
    if (units.empty()) {
      on_progress(1.0);
      return;
    }
    std::vector<Milliseconds> times;
    for (const int ms : qwen::FixTimestamps(aligner_.Align(
             request.pcm, units,
             [&](double fraction) { on_progress(kAlignFrom + (1.0 - kAlignFrom) * fraction); },
             stop))) {
      times.emplace_back(ms);
    }
    std::vector<Word> words = qwen::TimedWords(heard.text, heard.language, units, times);
    // Sentence by sentence, as the transcript shows them.
    std::vector<Word> sentence;
    for (Word& word : words) {
      const bool ends = EndsSentence(word.text);
      sentence.push_back(std::move(word));
      if (ends) on_words({.words = std::exchange(sentence, {}), .language = heard.language});
    }
    if (!sentence.empty()) on_words({.words = std::move(sentence), .language = heard.language});
    on_progress(1.0);
  }

 private:
  // Qwen3-ASR's transcript of the request, greedy as the package decodes.
  qwen::AsrOutput Listen(const TranscribeRequest& request, const FractionCallback& on_progress,
                         const StopToken& stop) {
    std::string context;
    for (const std::string& term : request.glossary) {
      if (!context.empty()) context += ", ";
      context += term;
    }
    std::string prompt = "<|im_start|>system\n" + context + "<|im_end|>\n<|im_start|>user\n" +
                         mtmd_default_marker() + "<|im_end|>\n<|im_start|>assistant\n";
    if (!request.language.empty()) {
      prompt += "language " + std::string(qwen::LanguageName(request.language)) + "<asr_text>";
    }
    std::vector<float> logits;
    asr_.Feed(
        prompt, request.pcm, qwen::AudioTokens(request.pcm.size()), {},
        [&](llama_token, const float* row) { logits.assign(row, row + asr_.row_size()); }, true,
        [&](double fraction) { on_progress(kEncodeShare * fraction); }, stop);

    const std::size_t limit = MaxTokens(request.pcm.size());
    const double expected =
        std::max(1.0, kTokensPerSecond * static_cast<double>(request.pcm.size()) / kSampleRate);
    std::vector<llama_token> tokens;
    std::string decoded;
    // Without a language, the output starts with one of language_starts_.
    std::size_t told = request.language.empty() ? 0 : SIZE_MAX;
    while (tokens.size() < limit) {
      llama_token next;
      if (told != SIZE_MAX) {
        next = AllowedLanguageToken(tokens, logits);
      } else {
        next = static_cast<llama_token>(ArgMax(logits.data(), logits.size()));
        if (llama_vocab_is_eog(asr_.vocab(), next)) break;
      }
      tokens.push_back(next);
      decoded += asr_.Piece(next);
      if (told != SIZE_MAX && LanguageStartDone(tokens)) told = SIZE_MAX;
      if (tokens.size() % 16 == 0 && EndsInLoop(tokens)) break;
      const float* row = asr_.Next(next, stop);
      logits.assign(row, row + asr_.row_size());
      on_progress(kEncodeShare + (kAlignFrom - kEncodeShare) *
                                     std::min(1.0, static_cast<double>(tokens.size()) / expected));
    }
    return qwen::ParseAsrOutput(decoded, request.language);
  }

  // The likeliest token that continues one of language_starts_.
  llama_token AllowedLanguageToken(std::span<const llama_token> tokens,
                                   std::span<const float> logits) const {
    llama_token best = -1;
    for (const std::vector<llama_token>& start : language_starts_) {
      if (start.size() <= tokens.size() || !std::equal(tokens.begin(), tokens.end(), start.begin())) {
        continue;
      }
      const llama_token candidate = start[tokens.size()];
      if (best < 0 || logits[static_cast<std::size_t>(candidate)] >
                          logits[static_cast<std::size_t>(best)]) {
        best = candidate;
      }
    }
    return best;
  }

  bool LanguageStartDone(std::span<const llama_token> tokens) const {
    return std::any_of(language_starts_.begin(), language_starts_.end(),
                       [&](const std::vector<llama_token>& start) {
                         return std::equal(start.begin(), start.end(), tokens.begin(), tokens.end());
                       });
  }

  LlamaAudio asr_;
  qwen::QwenAligner aligner_;
  ModelInfo info_;
  std::vector<std::vector<llama_token>> language_starts_;
};

}  // namespace

std::unique_ptr<Transcriber> OpenQwen(const QwenConfig& config) {
  return std::make_unique<QwenTranscriber>(config);
}

}  // namespace sillok
