#ifndef SILLOK_BACKENDS_QWEN_LLAMA_AUDIO_HPP_
#define SILLOK_BACKENDS_QWEN_LLAMA_AUDIO_HPP_

#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <llama.h>

#include "backends/ggml_devices.hpp"
#include "core/cancel.hpp"

struct mtmd_context;

namespace sillok::qwen {

struct LlamaAudioConfig {
  std::filesystem::path text_model;  // GGUF of the language model
  std::filesystem::path mmproj;      // GGUF of mtmd's audio encoder
  ComputeDevice device;
  int threads = 4;
  int context = 8192;       // positions: prompt, audio and output
  bool embeddings = false;  // rows are hidden states instead of logits
};

// A llama.cpp language model with mtmd's audio encoder in front of it: a
// prompt with audio goes in, and the rows of chosen positions come out.
class LlamaAudio {
 public:
  // Throws ModelError when a file does not load.
  explicit LlamaAudio(const LlamaAudioConfig& config);
  ~LlamaAudio();
  LlamaAudio(const LlamaAudio&) = delete;
  LlamaAudio& operator=(const LlamaAudio&) = delete;

  const llama_vocab* vocab() const { return vocab_; }
  // Length of a row: the vocabulary for logits, else the hidden size.
  int row_size() const { return row_size_; }

  // The one token `text` stands for (a special token such as "<asr_text>");
  // throws ModelError when it is not one.
  llama_token Token(std::string_view text) const;
  std::vector<llama_token> Tokenize(std::string_view text, bool parse_special) const;
  std::string Piece(llama_token token) const;
  // A number in the model file's metadata; throws ModelError without it.
  int MetaNumber(const char* key) const;

  // Starts anew with `prompt`, in which mtmd_default_marker() stands for
  // `pcm` (16 kHz mono). The audio gives `audio_tokens` positions (mtmd may
  // make more, of padding). Each prompt token after the audio that `want`
  // accepts passes its row to `on_row`; with `last_row`, the prompt's last
  // token passes its row too. `on_encoded` receives the fraction of the
  // audio encoded. Throws Cancelled once `stop` is requested.
  void Feed(std::string_view prompt, std::span<const float> pcm, std::size_t audio_tokens,
            const std::function<bool(llama_token)>& want,
            const std::function<void(llama_token, const float*)>& on_row, bool last_row,
            const std::function<void(double)>& on_encoded, const StopToken& stop);

  // Appends `token` and returns its row. Throws Cancelled once `stop` is
  // requested, ModelError when the context is full.
  const float* Next(llama_token token, const StopToken& stop);

 private:
  struct Batch;

  void Process(Batch& batch, const StopToken& stop);
  const float* Row(int index) const;

  std::unique_ptr<Batch> batch_;
  llama_model* model_ = nullptr;
  llama_context* context_ = nullptr;
  mtmd_context* mtmd_ = nullptr;
  const llama_vocab* vocab_ = nullptr;
  bool embeddings_ = false;
  int row_size_ = 0;
  int n_batch_ = 0;
  llama_pos position_ = 0;
  const StopToken* stop_ = nullptr;  // of the running call, for the abort callback
};

}  // namespace sillok::qwen

#endif  // SILLOK_BACKENDS_QWEN_LLAMA_AUDIO_HPP_
