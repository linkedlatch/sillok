#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <vector>

namespace sillok {

class WavReader {
public:
    explicit WavReader(const std::filesystem::path& path);

    double duration() const { return static_cast<double>(sampleCount_) / kRate; }
    std::vector<float> read(double start, double end);

private:
    static constexpr int kRate = 16000;

    std::ifstream stream_;
    std::mutex mutex_;
    std::streamoff dataOffset_ = 0;
    int64_t sampleCount_ = 0;
    bool isFloat_ = false;
};

}
