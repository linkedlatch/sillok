#include "transcriber.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <stdexcept>

#include "language.hpp"

namespace sillok {

StepResult ChunkTranscriber::step(const TranscriptionState& state, const std::vector<AudioChunk>& chunks,
                                  const AudioReader& read, Engine& engine, const CancelToken& cancel) const {
    if (cancel.isCancelled()) throw Cancelled();
    const AudioChunk& chunk = chunks.at(state.nextChunk);
    const std::vector<float> samples = read(chunk.start, chunk.end);
    if (samples.empty() && chunk.duration() > 0) throw std::runtime_error("failed to read audio");

    const std::string language = LanguageResolver{options.languageMode}.resolve(
        state.nextChunk, samples, chunks, state.previousLanguage, read, engine);
    const auto context = state.previousLanguage == language ? state.previousText : std::nullopt;
    const auto prompt = PromptBuilder(options.vocabulary, options.maxPromptTokens)
                            .build(context, [&](const std::string& text) { return engine.tokenCount(text); });

    std::vector<Segment> raw = engine.transcribe(samples, ChunkTranscribeOptions{language, prompt, options.beamSize}, cancel);
    std::vector<Segment> shifted;
    for (const auto& segment : filter.apply(raw)) shifted.push_back(offset(segment, chunk.start));
    std::vector<Segment> segments = splitter.split(trimToChunk(shifted, chunk));

    StepResult result{state, segments};
    result.state.nextChunk += 1;
    result.state.previousLanguage = language;
    bool unreliable = segments.empty();
    std::string text;
    for (const auto& segment : segments) {
        unreliable = unreliable || segment.isLowConfidence;
        if (!text.empty()) text += " ";
        text += segment.text;
    }
    result.state.previousText = unreliable ? std::nullopt : std::optional<std::string>(text);
    return result;
}

static bool hasMultipleSpeakers(const std::vector<Segment>& segments) {
    std::set<std::string> speakers;
    for (const auto& segment : segments) {
        if (segment.speaker) speakers.insert(*segment.speaker);
    }
    return speakers.size() > 1;
}

static std::string speakerLabel(const Segment& segment, bool show, const std::string& format) {
    if (!show || !segment.speaker) return "";
    const std::string placeholder = "{speaker}";
    const auto position = format.find(placeholder);
    if (position == std::string::npos) return format;
    return format.substr(0, position) + *segment.speaker + format.substr(position + placeholder.size());
}

std::string exportText(const std::vector<Segment>& segments, const std::string& speakerFormat) {
    const bool show = hasMultipleSpeakers(segments);
    std::string text;
    for (size_t i = 0; i < segments.size(); ++i) {
        if (i > 0) text += "\n";
        const auto label = speakerLabel(segments[i], show, speakerFormat);
        if (!label.empty()) text += label + ": ";
        text += segments[i].text;
    }
    return text + "\n";
}

std::string subRipTimestamp(double seconds) {
    const long long milliseconds = std::llround(std::max(seconds, 0.0) * 1000);
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%02lld:%02lld:%02lld,%03lld", milliseconds / 3600000, milliseconds / 60000 % 60,
                  milliseconds / 1000 % 60, milliseconds % 1000);
    return buffer;
}

std::string exportSubRip(const std::vector<Segment>& segments, const std::string& speakerFormat) {
    const bool show = hasMultipleSpeakers(segments);
    std::string text;
    for (size_t i = 0; i < segments.size(); ++i) {
        if (i > 0) text += "\n";
        const auto label = speakerLabel(segments[i], show, speakerFormat);
        text += std::to_string(i + 1) + "\n" + subRipTimestamp(segments[i].start) + " --> " +
                subRipTimestamp(segments[i].end) + "\n" + (label.empty() ? "" : "[" + label + "] ") + segments[i].text + "\n";
    }
    return text;
}

}
