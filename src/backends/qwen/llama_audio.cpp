#include "backends/qwen/llama_audio.hpp"

#include <algorithm>
#include <cstdlib>
#include <utility>

#include <mtmd.h>

#include "core/files.hpp"
#include "core/model_provider.hpp"

namespace sillok::qwen {
namespace {

// Rows wanted in one call: llama.cpp keeps logits for each (with embeddings
// too), 0.6 MB a row for Qwen's vocabulary.
constexpr int kMaxRowsPerCall = 256;

bool Aborted(void* data) {
  const auto* stop = *static_cast<const StopToken* const*>(data);
  return stop != nullptr && stop->StopRequested();
}

}  // namespace

struct LlamaAudio::Batch {
  explicit Batch(llama_context* context) : batch(llama_batch_ext_init(context)) {
    if (batch == nullptr) throw ModelError("cannot make a llama.cpp batch");
  }
  ~Batch() { llama_batch_ext_free(batch); }
  Batch(const Batch&) = delete;
  Batch& operator=(const Batch&) = delete;

  void Clear() {
    llama_batch_ext_clear(batch);
    size = 0;
    rows.clear();
  }

  llama_batch_ext* batch;
  int size = 0;
  std::vector<std::pair<int, llama_token>> rows;  // batch index, token
};

LlamaAudio::LlamaAudio(const LlamaAudioConfig& config) : embeddings_(config.embeddings) {
  ggml_backend_dev_t gpu = GgmlGpu(config.device);
  ggml_backend_dev_t devices[] = {gpu, nullptr};
  llama_model_params model_params = llama_model_default_params();
  model_params.devices = gpu != nullptr ? devices : nullptr;
  model_params.n_gpu_layers = gpu != nullptr ? -1 : 0;
  const std::string text_path = PathToUtf8(config.text_model);
  model_ = llama_model_load_from_file(text_path.c_str(), model_params);
  if (model_ == nullptr) throw ModelError("cannot load the model " + text_path);
  vocab_ = llama_model_get_vocab(model_);

  n_batch_ = 2048;
  llama_context_params context_params = llama_context_default_params();
  context_params.n_ctx = static_cast<uint32_t>(config.context);
  context_params.n_batch = static_cast<uint32_t>(n_batch_);
  context_params.n_ubatch = static_cast<uint32_t>(n_batch_);
  context_params.n_seq_max = 1;
  context_params.n_threads = config.threads;
  context_params.n_threads_batch = config.threads;
  context_params.embeddings = config.embeddings;
  context_params.pooling_type = LLAMA_POOLING_TYPE_NONE;
  context_params.no_perf = true;
  // On the CPU nothing goes to a GPU, not even large operations.
  context_params.op_offload = gpu != nullptr;
  context_params.offload_kqv = gpu != nullptr;
  context_ = llama_init_from_model(model_, context_params);
  if (context_ == nullptr) {
    llama_model_free(model_);
    throw ModelError("cannot make a llama.cpp context for " + text_path);
  }
  llama_set_abort_callback(context_, Aborted, &stop_);

  mtmd_context_params mtmd_params = mtmd_context_params_default();
  mtmd_params.use_gpu = gpu != nullptr;
  mtmd_params.device = gpu;
  mtmd_params.n_threads = config.threads;
  mtmd_params.print_timings = false;
  mtmd_params.warmup = false;
  const std::string mmproj_path = PathToUtf8(config.mmproj);
  mtmd_ = mtmd_init_from_file(mmproj_path.c_str(), model_, mtmd_params);
  if (mtmd_ == nullptr) {
    llama_free(context_);
    llama_model_free(model_);
    throw ModelError("cannot load the audio encoder " + mmproj_path);
  }
  row_size_ = embeddings_ ? llama_model_n_embd_out(model_) : llama_vocab_n_tokens(vocab_);
  batch_ = std::make_unique<Batch>(context_);
}

LlamaAudio::~LlamaAudio() {
  batch_.reset();
  mtmd_free(mtmd_);
  llama_free(context_);
  llama_model_free(model_);
}

llama_token LlamaAudio::Token(std::string_view text) const {
  const std::vector<llama_token> tokens = Tokenize(text, true);
  if (tokens.size() != 1) throw ModelError("the model has no token " + std::string(text));
  return tokens[0];
}

std::vector<llama_token> LlamaAudio::Tokenize(std::string_view text, bool parse_special) const {
  std::vector<llama_token> tokens(text.size() + 8);
  int count = llama_tokenize(vocab_, text.data(), static_cast<int32_t>(text.size()), tokens.data(),
                             static_cast<int32_t>(tokens.size()), false, parse_special);
  if (count < 0) {
    tokens.resize(static_cast<std::size_t>(-count));
    count = llama_tokenize(vocab_, text.data(), static_cast<int32_t>(text.size()), tokens.data(),
                           static_cast<int32_t>(tokens.size()), false, parse_special);
  }
  if (count < 0) throw ModelError("cannot tokenize text");
  tokens.resize(static_cast<std::size_t>(count));
  return tokens;
}

std::string LlamaAudio::Piece(llama_token token) const {
  char buffer[256];
  const int length = llama_token_to_piece(vocab_, token, buffer, sizeof(buffer), 0, true);
  if (length >= 0) return std::string(buffer, static_cast<std::size_t>(length));
  std::string piece(static_cast<std::size_t>(-length), '\0');
  llama_token_to_piece(vocab_, token, piece.data(), static_cast<int32_t>(piece.size()), 0, true);
  return piece;
}

int LlamaAudio::MetaNumber(const char* key) const {
  char value[32];
  if (llama_model_meta_val_str(model_, key, value, sizeof(value)) <= 0) {
    throw ModelError(std::string("the model file has no ") + key);
  }
  return std::atoi(value);
}

const float* LlamaAudio::Row(int index) const {
  return embeddings_ ? llama_get_embeddings_ith(context_, index)
                     : llama_get_logits_ith(context_, index);
}

void LlamaAudio::Process(Batch& batch, const StopToken& stop) {
  if (batch.size == 0) return;
  ThrowIfStopped(stop);
  const int32_t status = llama_process(context_, LLAMA_PROCESS_TYPE_DECODE, batch.batch);
  ThrowIfStopped(stop);
  if (status != 0) {
    throw ModelError("llama.cpp failed with " + std::to_string(status) +
                     (status == 1 ? " (context full)" : ""));
  }
}

void LlamaAudio::Feed(std::string_view prompt, std::span<const float> pcm, std::size_t audio_tokens,
                      const std::function<bool(llama_token)>& want,
                      const std::function<void(llama_token, const float*)>& on_row, bool last_row,
                      const std::function<void(double)>& on_encoded, const StopToken& stop) {
  stop_ = &stop;
  llama_memory_clear(llama_get_memory(context_), true);
  position_ = 0;

  std::unique_ptr<mtmd_bitmap, decltype(&mtmd_bitmap_free)> bitmap(
      mtmd_bitmap_init_from_audio(pcm.size(), pcm.data()), mtmd_bitmap_free);
  std::unique_ptr<mtmd_input_chunks, decltype(&mtmd_input_chunks_free)> chunks(
      mtmd_input_chunks_init(), mtmd_input_chunks_free);
  if (!bitmap || !chunks) throw ModelError("cannot prepare the audio for mtmd");
  const std::string text(prompt);
  const mtmd_input_text input{text.c_str(), text.size(), false, true};
  const mtmd_bitmap* bitmaps[] = {bitmap.get()};
  if (mtmd_tokenize(mtmd_, chunks.get(), &input, bitmaps, 1) != 0) {
    throw ModelError("mtmd cannot tokenize the prompt");
  }

  const std::size_t count = mtmd_input_chunks_size(chunks.get());
  std::size_t audio_chunks = 0;
  for (std::size_t c = 0; c < count; ++c) {
    if (mtmd_input_chunk_get_type(mtmd_input_chunks_get(chunks.get(), c)) !=
        MTMD_INPUT_CHUNK_TYPE_TEXT) {
      ++audio_chunks;
    }
  }

  Batch& batch = *batch_;
  batch.Clear();
  const auto flush = [&] {
    Process(batch, stop);
    for (const auto& [index, token] : batch.rows) on_row(token, Row(index));
    batch.Clear();
  };
  const int n_embd = llama_model_n_embd_inp(model_);
  std::vector<float> audio;  // encoded rows not yet fed
  bool after_audio = false;
  std::size_t encoded = 0;
  const auto feed_audio = [&] {
    // mtmd pads the last window; the model was trained without that.
    const std::size_t rows = std::min(audio.size() / static_cast<std::size_t>(n_embd), audio_tokens);
    for (std::size_t i = 0; i < rows; ++i) {
      if (batch.size == n_batch_) flush();
      const int index = llama_batch_ext_add_embd(
          batch.batch, 0, llama_embd{audio.data() + i * static_cast<std::size_t>(n_embd), 1,
                                     static_cast<std::size_t>(n_embd)});
      if (index < 0) throw ModelError("cannot add audio to a llama.cpp batch");
      // Audio has one position on every M-RoPE axis.
      const llama_pos position[4] = {position_, position_, position_, position_};
      llama_batch_ext_set_pos(batch.batch, index, position);
      ++position_;
      ++batch.size;
    }
    audio.clear();
  };

  for (std::size_t c = 0; c < count; ++c) {
    const mtmd_input_chunk* chunk = mtmd_input_chunks_get(chunks.get(), c);
    if (mtmd_input_chunk_get_type(chunk) != MTMD_INPUT_CHUNK_TYPE_TEXT) {
      ThrowIfStopped(stop);
      if (mtmd_encode_chunk(mtmd_, chunk) != 0) throw ModelError("mtmd cannot encode the audio");
      const float* rows = mtmd_get_output_embd(mtmd_);
      audio.insert(audio.end(), rows,
                   rows + mtmd_input_chunk_get_n_tokens(chunk) * static_cast<std::size_t>(n_embd));
      after_audio = true;
      if (on_encoded) on_encoded(static_cast<double>(++encoded) / static_cast<double>(audio_chunks));
      continue;
    }
    if (!audio.empty()) feed_audio();
    std::size_t n_tokens = 0;
    const llama_token* tokens = mtmd_input_chunk_get_tokens_text(chunk, &n_tokens);
    for (std::size_t i = 0; i < n_tokens; ++i) {
      if (batch.size == n_batch_ || static_cast<int>(batch.rows.size()) == kMaxRowsPerCall) flush();
      const int index = llama_batch_ext_add_token(batch.batch, 0, tokens[i]);
      if (index < 0) throw ModelError("cannot add a token to a llama.cpp batch");
      llama_batch_ext_set_pos(batch.batch, index, &position_);
      ++position_;
      ++batch.size;
      const bool last = last_row && c + 1 == count && i + 1 == n_tokens;
      if (last || (after_audio && want && want(tokens[i]))) {
        llama_batch_ext_set_output_logits(batch.batch, index, true);
        batch.rows.emplace_back(index, tokens[i]);
      }
    }
  }
  if (!audio.empty()) feed_audio();
  flush();
}

const float* LlamaAudio::Next(llama_token token, const StopToken& stop) {
  stop_ = &stop;
  Batch& batch = *batch_;
  batch.Clear();
  const int index = llama_batch_ext_add_token(batch.batch, 0, token);
  if (index < 0) throw ModelError("cannot add a token to a llama.cpp batch");
  llama_batch_ext_set_pos(batch.batch, index, &position_);
  llama_batch_ext_set_output_logits(batch.batch, index, true);
  ++position_;
  batch.size = 1;
  Process(batch, stop);
  return Row(index);
}

}  // namespace sillok::qwen
