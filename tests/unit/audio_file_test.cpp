#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <span>
#include <vector>

#include "core/audio_buffer.hpp"
#include "core/cancel.hpp"
#include "media/audio_file.hpp"
#include "media/audio_input.hpp"

namespace sillok {
namespace {

// The committed media files (tests/data/media): a second of a 440 Hz tone at
// -24 dBFS in each.
std::filesystem::path MediaFile(const char* name) {
  return std::filesystem::path(SILLOK_TEST_MEDIA_DIR) / name;
}

double Rms(std::span<const float> pcm) {
  double sum = 0.0;
  for (const float x : pcm) sum += static_cast<double>(x) * static_cast<double>(x);
  return std::sqrt(sum / static_cast<double>(pcm.size()));
}

// The file at `path` as a job keeps it; `progress` receives the last fraction
// read.
std::vector<float> Decode(const std::filesystem::path& path, AudioInput::Loudness loudness,
                          double* progress = nullptr) {
  AudioBuffer audio;
  AudioInput input(audio, loudness);
  DecodeAudioFile(
      path, input, [&](double fraction) { if (progress != nullptr) *progress = fraction; },
      StopToken());
  input.Finish();
  return audio.Read({0, audio.size()});
}

TEST(AudioFileTest, ReadsAudioAndVideoFiles) {
  for (const char* name : {"tone.m4a", "tone.mp3", "tone.opus", "tone.flac", "tone.webm",
                           "lecture.mp4"}) {
    SCOPED_TRACE(name);
    double last = -1;
    const std::vector<float> pcm = Decode(MediaFile(name), AudioInput::Loudness::kEvenedOut, &last);
    EXPECT_EQ(last, 1.0);
    // One second, without the encoders' priming and padding.
    EXPECT_NEAR(static_cast<double>(pcm.size()), 16000, 100);
    // Raised towards the filter's peak.
    EXPECT_GT(Rms(pcm), 0.3);
  }
}

TEST(AudioFileTest, CopyLinesUpWithTheRecording) {
  AudioBuffer audio;
  AudioInput input(audio);
  DecodeAudioFile(MediaFile("tone.flac"), input, {}, StopToken());
  input.Finish();
  const auto copy = std::filesystem::temp_directory_path() / "sillok media test copy.m4a";
  double last = -1;
  WriteAacFile(audio, copy, [&](double fraction) { last = fraction; }, StopToken());
  EXPECT_EQ(last, 1.0);

  const std::vector<float> original = audio.Read({0, audio.size()});
  const std::vector<float> decoded = Decode(copy, AudioInput::Loudness::kAsRecorded);
  std::filesystem::remove(copy);
  ASSERT_EQ(decoded.size(), original.size());
  double difference = 0;
  double energy = 0;
  for (std::size_t i = 2000; i < 14000; ++i) {
    const double d = static_cast<double>(decoded[i]) - static_cast<double>(original[i]);
    difference += d * d;
    energy += static_cast<double>(original[i]) * static_cast<double>(original[i]);
  }
  // Shifted by the encoder's priming, the tone would differ fully.
  EXPECT_LT(difference / energy, 0.05);
}

}  // namespace
}  // namespace sillok
