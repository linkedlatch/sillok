#ifndef SILLOK_BACKENDS_QWEN_QWEN_CHUNKS_HPP_
#define SILLOK_BACKENDS_QWEN_QWEN_CHUNKS_HPP_

#include <vector>

#include "core/audio.hpp"
#include "core/audio_buffer.hpp"

namespace sillok::qwen {

// What the qwen-asr package hears at once with word times: at most 180 s
// (MAX_FORCE_ALIGN_INPUT_SECONDS), the aligner's limit.
inline constexpr SampleIndex kMaxChunk = SampleIndex{kSampleRate} * 180;
// How far either side of each 180 s mark it looks for a place to cut.
inline constexpr SampleIndex kCutSearch = SampleIndex{kSampleRate} * 5;
// The stretch whose loudness it compares: 0.1 s.
inline constexpr SampleIndex kCutWindow = kSampleRate / 10;
// Shorter pieces are padded with silence to this length (MIN_ASR_INPUT_SECONDS).
inline constexpr SampleIndex kMinChunk = kSampleRate / 2;

// The recording in consecutive pieces, as qwen-asr 0.0.6 cuts it
// (split_audio_into_chunks): while more than 180 s are left, the next cut is
// at the quietest sample of the quietest 0.1 s within 5 s of the 180 s mark,
// so a piece may be up to 185 s. Together the pieces cover the recording
// with no gaps or overlaps; a recording of 180 s or less is one piece.
std::vector<SampleRange> AudioChunks(const AudioBuffer& audio);

}  // namespace sillok::qwen

#endif  // SILLOK_BACKENDS_QWEN_QWEN_CHUNKS_HPP_
