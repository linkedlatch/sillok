#include "models.hpp"

#include <array>
#include <cstdio>
#include <fstream>

namespace sillok {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

constexpr std::array<uint32_t, 64> kRoundConstants = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

uint32_t rotate(uint32_t value, int bits) {
    return (value >> bits) | (value << (32 - bits));
}

class Sha256 {
public:
    void update(const unsigned char* data, size_t size) {
        length_ += size;
        while (size > 0) {
            const size_t take = std::min(size, block_.size() - filled_);
            std::copy(data, data + take, block_.begin() + static_cast<std::ptrdiff_t>(filled_));
            filled_ += take;
            data += take;
            size -= take;
            if (filled_ == block_.size()) {
                compress();
                filled_ = 0;
            }
        }
    }

    std::string finish() {
        const uint64_t bits = length_ * 8;
        const unsigned char one = 0x80;
        update(&one, 1);
        const unsigned char zero = 0;
        while (filled_ != 56) update(&zero, 1);
        for (int shift = 56; shift >= 0; shift -= 8) {
            const auto byte = static_cast<unsigned char>(bits >> shift);
            update(&byte, 1);
        }
        std::string hex;
        char buffer[9];
        for (uint32_t word : state_) {
            std::snprintf(buffer, sizeof buffer, "%08x", word);
            hex += buffer;
        }
        return hex;
    }

private:
    std::array<uint32_t, 8> state_ = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::array<unsigned char, 64> block_{};
    size_t filled_ = 0;
    uint64_t length_ = 0;

    void compress() {
        std::array<uint32_t, 64> w{};
        for (int i = 0; i < 16; ++i) {
            w[i] = (uint32_t{block_[i * 4]} << 24) | (uint32_t{block_[i * 4 + 1]} << 16) | (uint32_t{block_[i * 4 + 2]} << 8) | block_[i * 4 + 3];
        }
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = rotate(w[i - 15], 7) ^ rotate(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotate(w[i - 2], 17) ^ rotate(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        auto [a, b, c, d, e, f, g, h] = state_;
        for (int i = 0; i < 64; ++i) {
            const uint32_t s1 = rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25);
            const uint32_t choice = (e & f) ^ (~e & g);
            const uint32_t first = h + s1 + choice + kRoundConstants[i] + w[i];
            const uint32_t s0 = rotate(a, 2) ^ rotate(a, 13) ^ rotate(a, 22);
            const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t second = s0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + first;
            d = c;
            c = b;
            b = a;
            a = first + second;
        }
        const std::array<uint32_t, 8> add = {a, b, c, d, e, f, g, h};
        for (int i = 0; i < 8; ++i) state_[i] += add[i];
    }
};

const char* kindName(ModelKind kind) {
    switch (kind) {
    case ModelKind::whisper: return "whisper";
    case ModelKind::coreMLEncoder: return "coreMLEncoder";
    case ModelKind::vad: return "vad";
    case ModelKind::diarization: return "diarization";
    }
    return "whisper";
}

ModelKind kindFromName(const std::string& name) {
    if (name == "coreMLEncoder") return ModelKind::coreMLEncoder;
    if (name == "vad") return ModelKind::vad;
    if (name == "diarization") return ModelKind::diarization;
    if (name == "whisper") return ModelKind::whisper;
    throw std::runtime_error("unknown model kind: " + name);
}

bool equalsIgnoringCase(const std::string& a, const std::string& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return std::tolower(x) == std::tolower(y); });
}

}

std::string ModelEntry::installedName() const {
    const std::string suffix = ".zip";
    if (kind == ModelKind::coreMLEncoder && fileName.size() > suffix.size() && fileName.ends_with(suffix)) {
        return fileName.substr(0, fileName.size() - suffix.size());
    }
    return fileName;
}

const ModelEntry* ModelManifest::entry(const std::string& id) const {
    for (const auto& model : models) {
        if (model.id == id) return &model;
    }
    return nullptr;
}

const ModelEntry* ModelManifest::coreMLEncoder(const ModelEntry& whisper) const {
    return whisper.coreMLEncoderId ? entry(*whisper.coreMLEncoderId) : nullptr;
}

std::vector<ModelEntry> ModelSelection::required(const ModelManifest& manifest, const std::string& whisperId, bool useCoreML, bool diarization) {
    const ModelEntry* whisper = manifest.entry(whisperId);
    if (!whisper) return {};
    std::vector<ModelEntry> entries{*whisper};
    if (useCoreML) {
        if (const ModelEntry* encoder = manifest.coreMLEncoder(*whisper)) entries.push_back(*encoder);
    }
    if (const ModelEntry* vad = manifest.entry(vadId)) entries.push_back(*vad);
    if (diarization) {
        if (const ModelEntry* diarizer = manifest.entry(diarizationId)) entries.push_back(*diarizer);
    }
    return entries;
}

bool ModelSelection::shouldUseCoreML(const ModelEntry& whisper, const ModelEntry* encoder, int64_t availableMemory, bool countEncoder) {
    if (!encoder) return false;
    return availableMemory >= whisper.size + (countEncoder ? encoder->size : 0) + runtimeOverhead;
}

ChecksumMismatch::ChecksumMismatch(const std::string& fileName)
    : std::runtime_error(fileName + " is corrupted; download it again") {}

std::string sha256Hex(const fs::path& file) {
    std::ifstream input(file, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open file: " + utf8String(file.filename()));
    Sha256 hasher;
    std::vector<char> buffer(4 << 20);
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        hasher.update(reinterpret_cast<const unsigned char*>(buffer.data()), static_cast<size_t>(input.gcount()));
    }
    return hasher.finish();
}

bool isInstalled(const ModelEntry& entry, const fs::path& root) {
    const fs::path location = root / utf8Path(entry.installedName());
    std::error_code error;
    if (entry.kind == ModelKind::coreMLEncoder) return fs::is_directory(location, error);
    return fs::is_regular_file(location, error) && static_cast<int64_t>(fs::file_size(location, error)) == entry.size;
}

void installVerified(const fs::path& file, const ModelEntry& entry, const fs::path& root) {
    if (!equalsIgnoringCase(sha256Hex(file), entry.sha256)) {
        std::error_code ignored;
        fs::remove(file, ignored);
        throw ChecksumMismatch(entry.fileName);
    }
    fs::create_directories(root);
    fs::rename(file, root / utf8Path(entry.fileName));
}

void to_json(json& j, const ModelEntry& value) {
    j = json{{"id", value.id}, {"kind", kindName(value.kind)}, {"displayName", value.displayName}, {"fileName", value.fileName},
             {"size", value.size}, {"sha256", value.sha256}, {"urls", value.urls}, {"license", value.license}};
    if (value.coreMLEncoderId) j["coreMLEncoderID"] = *value.coreMLEncoderId;
}

void from_json(const json& j, ModelEntry& value) {
    j.at("id").get_to(value.id);
    value.kind = kindFromName(j.at("kind").get<std::string>());
    j.at("displayName").get_to(value.displayName);
    j.at("fileName").get_to(value.fileName);
    j.at("size").get_to(value.size);
    j.at("sha256").get_to(value.sha256);
    j.at("urls").get_to(value.urls);
    value.license = j.value("license", std::string());
    if (auto it = j.find("coreMLEncoderID"); it != j.end() && !it->is_null()) value.coreMLEncoderId = it->get<std::string>();
}

void to_json(json& j, const ModelManifest& value) {
    j = json{{"version", value.version}, {"models", value.models}};
}

void from_json(const json& j, ModelManifest& value) {
    value.version = j.value("version", 1);
    j.at("models").get_to(value.models);
}

}
