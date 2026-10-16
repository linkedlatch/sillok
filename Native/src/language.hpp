#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "types.hpp"

namespace sillok {

struct LanguageResolver {
    LanguageMode mode;
    double shortChunk = 3;
    std::string fallbackLanguage = "ko";
    double switchConfidence = 0.8;

    std::string resolve(int chunkIndex, const std::vector<float>& samples, const std::vector<AudioChunk>& chunks,
                        const std::optional<std::string>& previousLanguage, const AudioReader& read, Engine& engine) const;
};

struct PromptBuilder {
    static constexpr int hardTokenLimit = 224;

    std::vector<std::string> vocabulary;
    int maxTokens = 150;

    PromptBuilder(std::vector<std::string> vocabulary, int maxTokens);

    std::optional<std::string> build(const std::optional<std::string>& previousText,
                                     const std::function<int(const std::string&)>& countTokens) const;
};

}
