#include "language.hpp"

#include <algorithm>
#include <sstream>

namespace sillok {

std::string LanguageResolver::resolve(int chunkIndex, const std::vector<float>& samples, const std::vector<AudioChunk>& chunks,
                                      const std::optional<std::string>& previousLanguage, const AudioReader& read,
                                      Engine& engine) const {
    if (mode.kind == LanguageMode::Kind::fixed) return mode.code;
    const auto allowed = mode.allowedLanguages();
    const AudioChunk& chunk = chunks[chunkIndex];

    if (chunk.duration() >= shortChunk) {
        LanguageDetection detection = engine.detectLanguage(samples, allowed);
        if (!previousLanguage || detection.language == *previousLanguage) return detection.language;
        if (detection.confidence < switchConfidence) return *previousLanguage;
        if (chunkIndex + 1 >= static_cast<int>(chunks.size())) return detection.language;
        const AudioChunk& next = chunks[chunkIndex + 1];
        LanguageDetection confirmation = engine.detectLanguage(read(next.start, next.end), allowed);
        bool confirmed = confirmation.language == detection.language && confirmation.confidence >= switchConfidence;
        return confirmed ? detection.language : *previousLanguage;
    }
    if (previousLanguage) return *previousLanguage;

    std::vector<float> combined = samples;
    const size_t needed = static_cast<size_t>(shortChunk * kSampleRate);
    for (size_t index = chunkIndex + 1; combined.size() < needed && index < chunks.size(); ++index) {
        auto more = read(chunks[index].start, chunks[index].end);
        combined.insert(combined.end(), more.begin(), more.end());
    }
    if (combined.size() < needed) return fallbackLanguage;
    return engine.detectLanguage(combined, allowed).language;
}

PromptBuilder::PromptBuilder(std::vector<std::string> vocabulary, int maxTokens)
    : vocabulary(std::move(vocabulary)), maxTokens(std::min(maxTokens, hardTokenLimit)) {}

static std::string joined(const std::vector<std::string>& parts, const std::string& separator) {
    std::string result;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) result += separator;
        result += parts[i];
    }
    return result;
}

static std::string combine(const std::string& terms, const std::string& tail) {
    if (terms.empty()) return tail;
    if (tail.empty()) return terms;
    return terms + " " + tail;
}

std::optional<std::string> PromptBuilder::build(const std::optional<std::string>& previousText,
                                                const std::function<int(const std::string&)>& countTokens) const {
    std::vector<std::string> terms;
    for (const auto& term : vocabulary) {
        if (!term.empty()) terms.push_back(term);
    }
    std::string termText = joined(terms, ", ");
    while (!terms.empty() && countTokens(termText) > maxTokens) {
        terms.pop_back();
        termText = joined(terms, ", ");
    }

    std::vector<std::string> words;
    std::istringstream stream(previousText.value_or(""));
    for (std::string word; std::getline(stream, word, ' ');) {
        if (!word.empty()) words.push_back(word);
    }
    auto tail = [&](size_t count) {
        return joined(std::vector<std::string>(words.end() - static_cast<long>(count), words.end()), " ");
    };

    size_t low = 0;
    size_t high = words.size();
    while (low < high) {
        size_t mid = (low + high + 1) / 2;
        if (countTokens(combine(termText, tail(mid))) <= maxTokens) {
            low = mid;
        } else {
            high = mid - 1;
        }
    }
    std::string prompt = combine(termText, tail(low));
    if (prompt.empty()) return std::nullopt;
    return prompt;
}

}
