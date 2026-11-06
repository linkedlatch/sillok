#include "backends/whisper/whisper_transcriber.hpp"

#include <algorithm>
#include <exception>
#include <fstream>
#include <vector>

#include <whisper.h>

#include "core/cancel.hpp"
#include "core/files.hpp"
#include "core/model_provider.hpp"

namespace sillok {
namespace {

// What the callbacks of one whisper_full call need.
struct Run {
  const TokenSegmentCallback& on_segment;
  const StopToken& stop;
  std::exception_ptr error;
};

bool Aborted(void* data) {
  const Run* run = static_cast<Run*>(data);
  return run->error != nullptr || run->stop.StopRequested();
}

void NewSegments(whisper_context* context, whisper_state*, int count,
                 void* data) {
  Run* run = static_cast<Run*>(data);
  if (run->error) return;
  // Special tokens (timestamps, end of text, ...) have ids from EOT up.
  const whisper_token end_of_text = whisper_token_eot(context);
  const int total = whisper_full_n_segments(context);
  try {
    for (int i = total - count; i < total; ++i) {
      TokenSegment segment;
      for (int j = 0; j < whisper_full_n_tokens(context, i); ++j) {
        const whisper_token_data token = whisper_full_get_token_data(context, i, j);
        if (token.id >= end_of_text) continue;
        segment.tokens.push_back({.text = whisper_full_get_token_text(context, i, j),
                                  .start = Milliseconds{token.t0 * 10},
                                  .end = Milliseconds{token.t1 * 10},
                                  .probability = token.p});
      }
      if (!segment.tokens.empty()) run->on_segment(segment);
    }
  } catch (...) {
    run->error = std::current_exception();  // not through whisper.cpp
  }
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
    info_ = {.name = config.name,
             .device = config.device.name +
                       (use_path && config.core_ml ? " + Core ML encoder" : ""),
             .fell_back_to_cpu = config.device.fell_back_to_cpu};
  }

  ModelInfo Info() const override { return info_; }

  bool DetectsLanguage() const override {
    return whisper_is_multilingual(context_.get()) != 0;
  }

  std::vector<LanguageScore> DetectLanguage(
      std::span<const float> pcm, std::span<const std::string> candidates,
      StopToken stop) override {
    ThrowIfStopped(stop);
    if (whisper_pcm_to_mel(context_.get(), pcm.data(),
                           static_cast<int>(pcm.size()), threads_) != 0) {
      throw ModelError("whisper_pcm_to_mel failed");
    }
    std::vector<float> probabilities(
        static_cast<std::size_t>(whisper_lang_max_id() + 1), 0.0f);
    const int status = whisper_lang_auto_detect(context_.get(), 0, threads_,
                                                probabilities.data());
    ThrowIfStopped(stop);
    if (status < 0) throw ModelError("whisper_lang_auto_detect failed");
    std::vector<LanguageScore> scores;
    for (const std::string& language : candidates) {
      const int id = whisper_lang_id(language.c_str());
      scores.push_back(
          {language, id < 0 ? 0.0f : probabilities[static_cast<std::size_t>(id)]});
    }
    return scores;
  }

  void Transcribe(const TranscribeRequest& request,
                  const TokenSegmentCallback& on_segment,
                  StopToken stop) override {
    // whisper_full checks the abort callback only after its encoder pass.
    ThrowIfStopped(stop);
    whisper_full_params params =
        whisper_full_default_params(WHISPER_SAMPLING_BEAM_SEARCH);
    params.n_threads = threads_;
    params.language = request.language.c_str();
    params.print_progress = false;
    params.print_realtime = false;
    params.print_timestamps = false;
    params.token_timestamps = true;
    // Earlier text as context lets a mistake repeat for the rest of a
    // recording, so whisper_full gets room for the prompt alone.
    params.n_max_text_ctx = 0;
    if (!request.prompt.empty()) {
      params.initial_prompt = request.prompt.c_str();
      params.carry_initial_prompt = true;
      params.n_max_text_ctx = CountTokens(request.prompt) + 1;
    }
    Run run{.on_segment = on_segment, .stop = stop, .error = nullptr};
    params.abort_callback = Aborted;
    params.abort_callback_user_data = &run;
    params.new_segment_callback = NewSegments;
    params.new_segment_callback_user_data = &run;

    const int status = whisper_full(context_.get(), params, request.pcm.data(),
                                    static_cast<int>(request.pcm.size()));
    if (run.error) std::rethrow_exception(run.error);
    ThrowIfStopped(stop);
    if (status != 0) {
      throw ModelError("whisper_full failed with " + std::to_string(status));
    }
  }

 private:
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
  ModelInfo info_;
};

}  // namespace

std::unique_ptr<Transcriber> OpenWhisper(const WhisperConfig& config) {
  return std::make_unique<WhisperTranscriber>(config);
}

}  // namespace sillok
