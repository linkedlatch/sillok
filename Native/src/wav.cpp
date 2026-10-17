#include "wav.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

namespace sillok {

namespace {

constexpr int kPcm = 1;
constexpr int kFloat = 3;
constexpr int kExtensible = 0xFFFE;

template <typename T>
T readValue(std::ifstream& stream) {
    T value{};
    stream.read(reinterpret_cast<char*>(&value), sizeof value);
    return value;
}

std::string readTag(std::ifstream& stream) {
    char tag[4] = {};
    stream.read(tag, 4);
    return std::string(tag, 4);
}

}

WavReader::WavReader(const std::filesystem::path& path) : stream_(path, std::ios::binary) {
    if (!stream_) throw std::runtime_error("파일을 열 수 없습니다");
    if (readTag(stream_) != "RIFF") throw std::invalid_argument("WAV 파일이 아닙니다");
    readValue<uint32_t>(stream_);
    if (readTag(stream_) != "WAVE") throw std::invalid_argument("WAV 파일이 아닙니다");

    int format = 0, channels = 0, rate = 0, bits = 0;
    int64_t dataLength = -1;
    stream_.seekg(0, std::ios::end);
    const std::streamoff fileSize = stream_.tellg();
    stream_.seekg(12);
    while (stream_ && static_cast<std::streamoff>(stream_.tellg()) + 8 <= fileSize) {
        const std::string id = readTag(stream_);
        const uint32_t size = readValue<uint32_t>(stream_);
        const std::streamoff next = static_cast<std::streamoff>(stream_.tellg()) + static_cast<std::streamoff>(size) + (size & 1);
        if (id == "fmt ") {
            format = readValue<uint16_t>(stream_);
            channels = readValue<uint16_t>(stream_);
            rate = static_cast<int>(readValue<uint32_t>(stream_));
            readValue<uint32_t>(stream_);
            readValue<uint16_t>(stream_);
            bits = readValue<uint16_t>(stream_);
            if (format == kExtensible && size >= 40) {
                readValue<uint16_t>(stream_);
                readValue<uint16_t>(stream_);
                readValue<uint32_t>(stream_);
                format = readValue<uint16_t>(stream_);
            }
        } else if (id == "data") {
            dataOffset_ = static_cast<std::streamoff>(stream_.tellg());
            dataLength = std::min<int64_t>(size, fileSize - dataOffset_);
            break;
        }
        stream_.seekg(next);
    }

    if (dataLength < 0) throw std::invalid_argument("WAV 데이터가 없습니다");
    if (rate != kRate || channels != 1) throw std::invalid_argument("16kHz 모노 WAV가 아닙니다");
    isFloat_ = format == kFloat && bits == 32;
    if (!isFloat_ && !(format == kPcm && bits == 16)) throw std::invalid_argument("지원하지 않는 WAV 형식입니다");
    sampleCount_ = dataLength / (isFloat_ ? 4 : 2);
}

std::vector<float> WavReader::read(double start, double end) {
    const auto clampSample = [this](double seconds) {
        return std::clamp<int64_t>(static_cast<int64_t>(std::llround(seconds * kRate)), 0, sampleCount_);
    };
    const int64_t first = clampSample(start);
    const int64_t last = clampSample(end);
    if (last <= first) return {};
    const int bytesPerSample = isFloat_ ? 4 : 2;
    std::vector<char> bytes(static_cast<size_t>((last - first) * bytesPerSample));
    {
        std::lock_guard lock(mutex_);
        stream_.clear();
        stream_.seekg(dataOffset_ + first * bytesPerSample);
        stream_.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    std::vector<float> samples(static_cast<size_t>(last - first));
    if (isFloat_) {
        std::memcpy(samples.data(), bytes.data(), bytes.size());
    } else {
        for (size_t i = 0; i < samples.size(); ++i) {
            int16_t value;
            std::memcpy(&value, bytes.data() + i * 2, 2);
            samples[i] = value / 32768.0f;
        }
    }
    return samples;
}

}
