#include "backends/whisper/whisper_transcriber.hpp"

#include <algorithm>
#include <fstream>
#include <vector>

#include <whisper.h>

#include "core/cancel.hpp"
#include "core/files.hpp"
#include "core/model_provider.hpp"

namespace sillok {
namespace {

constexpr Millis kMaxAudio{30000};

bool Aborted(void* stop) {
  return static_cast<std::stop_token*>(stop)->stop_requested();
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

  TranscriberCapabilities Capabilities() const override {
    return {.max_audio = kMaxAudio,
            .detects_language = whisper_is_multilingual(context_.get()) != 0,
            .max_prompt_tokens = whisper_n_text_ctx(context_.get()) / 2};
  }

  std::vector<LanguageScore> DetectLanguage(
      std::span<const float> pcm, std::span<const std::string> candidates,
      std::stop_token stop) override {
    ThrowIfStopped(stop);
    if (whisper_pcm_to_mel(context_.get(), pcm.data(),
                           static_cast<int>(pcm.size()), threads_) != 0) {
      throw ModelError("whisper_pcm_to_mel failed");
    }
    std::vector<float> probabilities(
        static_cast<std::size_t>(whisper_lang_max_id() + 1), 0.0f);
    if (whisper_lang_auto_detect(context_.get(), 0, threads_,
                                 probabilities.data()) < 0) {
      throw ModelError("whisper_lang_auto_detect failed");
    }
    ThrowIfStopped(stop);
    std::vector<LanguageScore> scores;
    for (const std::string& language : candidates) {
      const int id = whisper_lang_id(language.c_str());
      scores.push_back(
          {language, id < 0 ? 0.0f : probabilities[static_cast<std::size_t>(id)]});
    }
    return scores;
  }

  int CountTokens(std::string_view text) const override {
    const std::string copy(text);
    std::vector<whisper_token> tokens(copy.size() + 16);
    const int count = whisper_tokenize(context_.get(), copy.c_str(),
                                       tokens.data(),
                                       static_cast<int>(tokens.size()));
    if (count < 0) throw ModelError("whisper_tokenize failed");
    return count;
  }

  std::vector<TokenSegment> Transcribe(const TranscribeRequest& request,
                                       std::stop_token stop) override {
    // whisper_full checks the abort callback only after its encoder pass.
    ThrowIfStopped(stop);
    whisper_full_params params =
        whisper_full_default_params(WHISPER_SAMPLING_BEAM_SEARCH);
    params.n_threads = threads_;
    params.language = request.language.c_str();
    params.no_context = true;  // the pipeline chooses the context
    params.print_progress = false;
    params.print_realtime = false;
    params.print_timestamps = false;
    params.token_timestamps = true;
    params.suppress_nst = true;
    params.initial_prompt =
        request.prompt.empty() ? nullptr : request.prompt.c_str();
    params.abort_callback = Aborted;
    params.abort_callback_user_data = &stop;

    whisper_context* context = context_.get();
    const int status = whisper_full(context, params, request.pcm.data(),
                                    static_cast<int>(request.pcm.size()));
    ThrowIfStopped(stop);
    if (status != 0) {
      throw ModelError("whisper_full failed with " + std::to_string(status));
    }

    // Special tokens (timestamps, end of text, ...) have ids from EOT up.
    const whisper_token end_of_text = whisper_token_eot(context);
    std::vector<TokenSegment> segments;
    for (int i = 0; i < whisper_full_n_segments(context); ++i) {
      TokenSegment segment;
      for (int j = 0; j < whisper_full_n_tokens(context, i); ++j) {
        const whisper_token_data data = whisper_full_get_token_data(context, i, j);
        if (data.id >= end_of_text) continue;
        segment.tokens.push_back({.text = whisper_full_get_token_text(context, i, j),
                                  .start = Millis{data.t0 * 10},
                                  .end = Millis{data.t1 * 10},
                                  .probability = data.p});
      }
      if (!segment.tokens.empty()) segments.push_back(std::move(segment));
    }
    return segments;
  }

 private:
  std::unique_ptr<whisper_context, decltype(&whisper_free)> context_;
  int threads_;
  ModelInfo info_;
};

}  // namespace

std::unique_ptr<Transcriber> OpenWhisper(const WhisperConfig& config) {
  return std::make_unique<WhisperTranscriber>(config);
}

}  // namespace sillok
