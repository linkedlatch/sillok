#include "backends/whisper/whisper_transcriber.hpp"

#include <algorithm>
#include <exception>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <whisper.h>

#include "backends/whisper/whisper_words.hpp"
#include "core/cancel.hpp"
#include "core/files.hpp"
#include "core/model_provider.hpp"

namespace sillok {
namespace {

// A language's code in whisper.cpp and back: Javanese is "jw" there and "jv"
// in sillok (ISO 639-1); the others are the same.
std::string ToWhisper(const std::string& code) { return code == "jv" ? "jw" : code; }
std::string FromWhisper(std::string_view code) { return code == "jw" ? "jv" : std::string(code); }

// What the callbacks of one whisper_full call need.
struct Run {
  const WordsCallback& on_words;
  const FractionCallback& on_progress;
  const StopToken& stop;
  SampleIndex samples = 0;  // of the request audio
  int handed = 0;  // segments passed to on_words so far
  std::exception_ptr error;
};

bool Aborted(void* data) {
  const Run* run = static_cast<Run*>(data);
  return run->error != nullptr || run->stop.StopRequested();
}

// Passes on the segments whisper.cpp finished since the last call.
void HandOver(whisper_context* context, Run& run) {
  if (run.error) return;
  // Special tokens (timestamps, end of text, ...) have ids from EOT up.
  const whisper_token end_of_text = whisper_token_eot(context);
  const int total = whisper_full_n_segments(context);
  try {
    for (; run.handed < total; ++run.handed) {
      const int i = run.handed;
      // The language it was told to decode in.
      const std::string language = FromWhisper(whisper_lang_str(whisper_full_lang_id(context)));
      std::vector<Token> tokens;
      const int count = whisper_full_n_tokens(context, i);
      for (int j = 0; j < count; ++j) {
        const whisper_token_data token = whisper_full_get_token_data(context, i, j);
        if (token.id >= end_of_text) continue;
        // whisper.cpp's token times, from the timestamp tokens around them.
        tokens.push_back({.text = whisper_full_get_token_text(context, i, j),
                          .start = Milliseconds{token.t0 * 10},
                          .end = Milliseconds{std::max(token.t0, token.t1) * 10}});
      }
      std::vector<Word> words = AssembleWords(tokens, language);
      if (words.empty()) continue;
      const SampleIndex heard = std::min(ToSamples(words.back().end), run.samples);
      run.on_words({.words = std::move(words), .language = language});
      if (run.samples > 0) {
        run.on_progress(static_cast<double>(heard) / static_cast<double>(run.samples));
      }
    }
  } catch (...) {
    run.error = std::current_exception();  // not through whisper.cpp
  }
}

void NewSegments(whisper_context* context, whisper_state*, int, void* data) {
  HandOver(context, *static_cast<Run*>(data));
}

// Loads through a reader instead of a path. whisper.cpp derives the Core ML
// encoder location from the model path, so without one it cannot find the
// encoder and runs its own (WHISPER_COREML_ALLOW_FALLBACK).
whisper_context* LoadWithoutPath(const std::filesystem::path& model,
                                 whisper_context_params params) {
  std::ifstream in(model, std::ios::binary);
  if (!in) return nullptr;
  whisper_model_loader loader{
      .context = &in,
      .read = [](void* context, void* output, size_t size) -> size_t {
        auto& stream = *static_cast<std::ifstream*>(context);
        stream.read(static_cast<char*>(output),
                    static_cast<std::streamsize>(size));
        return static_cast<size_t>(stream.gcount());
      },
      .eof = [](void* context) {
        return static_cast<std::ifstream*>(context)->eof();
      },
      .close = [](void*) {},
  };
  return whisper_init_with_params(&loader, params);
}

class WhisperTranscriber final : public Transcriber {
 public:
  explicit WhisperTranscriber(const WhisperConfig& config)
      : context_(nullptr, whisper_free), threads_(config.threads) {
    whisper_context_params params = whisper_context_default_params();
    params.use_gpu = config.device.gpu_index >= 0;
    params.gpu_device = std::max(0, config.device.gpu_index);
    const std::string path = PathToUtf8(config.model);
#ifdef SILLOK_COREML
    const bool use_path = config.core_ml;
#else
    const bool use_path = true;
#endif
    context_.reset(use_path
                       ? whisper_init_from_file_with_params(path.c_str(), params)
                       : LoadWithoutPath(config.model, params));
    if (!context_) throw ModelError("cannot load the Whisper model " + path);
    for (const std::string& language : config.languages) {
      const int id = whisper_lang_id(ToWhisper(language).c_str());
      if (id < 0) throw ModelError("Whisper does not know the language " + language);
      language_ids_.push_back(id);
    }
    info_ = {.name = config.name,
             .device = config.device.name +
                       (use_path && config.core_ml ? " + Core ML encoder" : ""),
             .fell_back_to_cpu = config.device.fell_back_to_cpu};
  }

  ModelInfo Info() const override { return info_; }
  // One window of the encoder.
  Milliseconds MaxAudio() const override { return Milliseconds{30000}; }

  // whisper_full keeps the prompt to half the text context, a token of which
  // marks it, and silently drops the start of a longer one. The other half
  // is for its own text, whatever the audio.
  void CheckGlossary(std::span<const std::string> glossary, Milliseconds) const override {
    const std::string prompt = JoinGlossary(glossary);
    if (prompt.empty()) return;
    const int room = whisper_n_text_ctx(context_.get()) / 2 - 1;
    const int count = CountTokens(prompt);
    if (count > room) {
      throw std::invalid_argument("glossary is " + std::to_string(count) +
                                  " tokens; this model takes at most " + std::to_string(room));
    }
  }

  void Transcribe(const TranscribeRequest& request,
                  const WordsCallback& on_words,
                  const FractionCallback& on_progress,
                  StopToken stop) override {
    // whisper_full checks the abort callback only after its encoder pass.
    ThrowIfStopped(stop);
    whisper_full_params params =
        whisper_full_default_params(WHISPER_SAMPLING_BEAM_SEARCH);
    params.n_threads = threads_;
    const std::string language =
        ToWhisper(request.language.empty() ? DetectLanguage(request.pcm) : request.language);
    ThrowIfStopped(stop);
    params.language = language.c_str();
    params.print_progress = false;
    params.print_realtime = false;
    params.print_timestamps = false;
    // Word times from whisper.cpp's token times (docs/DESIGN.md D45).
    params.token_timestamps = true;
    // Earlier text as context lets a mistake repeat for the rest of a
    // recording, so whisper_full gets room for the prompt alone.
    params.n_max_text_ctx = 0;
    const std::string prompt = JoinGlossary(request.glossary);
    if (!prompt.empty()) {
      params.initial_prompt = prompt.c_str();
      params.carry_initial_prompt = true;
      params.n_max_text_ctx = CountTokens(prompt) + 1;
    }
    Run run{.on_words = on_words,
            .on_progress = on_progress,
            .stop = stop,
            .samples = static_cast<SampleIndex>(request.pcm.size()),
            .error = nullptr};
    params.abort_callback = Aborted;
    params.abort_callback_user_data = &run;
    params.new_segment_callback = NewSegments;
    params.new_segment_callback_user_data = &run;

    const int status = whisper_full(context_.get(), params, request.pcm.data(),
                                    static_cast<int>(request.pcm.size()));
    if (status == 0) HandOver(context_.get(), run);
    if (run.error) std::rethrow_exception(run.error);
    ThrowIfStopped(stop);
    if (status != 0) {
      throw ModelError("whisper_full failed with " + std::to_string(status));
    }
  }

 private:
  // The most likely of the configured languages (the catalog lists all of
  // Whisper's), from the request (one encoder window), at the cost of one
  // more encoder pass, as whisper.cpp's "auto" does; this takes the same
  // probabilities.
  std::string DetectLanguage(std::span<const float> pcm) const {
    if (whisper_pcm_to_mel(context_.get(), pcm.data(), static_cast<int>(pcm.size()), threads_) != 0) {
      throw ModelError("whisper_pcm_to_mel failed");
    }
    std::vector<float> probabilities(static_cast<std::size_t>(whisper_lang_max_id()) + 1, 0.0f);
    if (whisper_lang_auto_detect(context_.get(), 0, threads_, probabilities.data()) < 0) {
      throw ModelError("whisper_lang_auto_detect failed");
    }
    const auto best = std::max_element(
        language_ids_.begin(), language_ids_.end(),
        [&](int a, int b) {
          return probabilities[static_cast<std::size_t>(a)] < probabilities[static_cast<std::size_t>(b)];
        });
    if (best == language_ids_.end()) throw ModelError("no languages to tell apart");
    return FromWhisper(whisper_lang_str(*best));
  }

  // Length of `text` in prompt tokens.
  int CountTokens(const std::string& text) const {
    std::vector<whisper_token> tokens(text.size() + 16);
    const int count = whisper_tokenize(context_.get(), text.c_str(), tokens.data(),
                                       static_cast<int>(tokens.size()));
    if (count < 0) throw ModelError("whisper_tokenize failed");
    return count;
  }

  std::unique_ptr<whisper_context, decltype(&whisper_free)> context_;
  int threads_;
  std::vector<int> language_ids_;  // whisper.cpp's ids of the configured languages
  ModelInfo info_;
};

}  // namespace

std::unique_ptr<Transcriber> OpenWhisper(const WhisperConfig& config) {
  return std::make_unique<WhisperTranscriber>(config);
}

}  // namespace sillok
