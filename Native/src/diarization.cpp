#include "diarization.hpp"

#include <algorithm>
#include <atomic>
#include <map>
#include <unordered_set>

#include "diarizer.h"
#include "segments.hpp"
#include "whisper_engine.hpp"

namespace sillok {

namespace {

int speakerFor(double start, double end, const std::vector<SpeakerTurn>& turns) {
    std::map<int, double> overlap;
    for (const auto& turn : turns) {
        const double shared = std::min(end, turn.end) - std::max(start, turn.start);
        if (shared > 0) overlap[turn.speaker] += shared;
    }
    if (!overlap.empty()) {
        return std::max_element(overlap.begin(), overlap.end(), [](const auto& a, const auto& b) { return a.second < b.second; })->first;
    }
    const double middle = (start + end) / 2;
    auto distance = [middle](const SpeakerTurn& turn) {
        return middle < turn.start ? turn.start - middle : middle > turn.end ? middle - turn.end : 0.0;
    };
    return std::min_element(turns.begin(), turns.end(), [&](const auto& a, const auto& b) { return distance(a) < distance(b); })->speaker;
}

struct Run {
    size_t first;
    size_t last;
    int speaker;
};

std::vector<Run> speakerRuns(const std::vector<WordTimestamp>& words, const std::vector<SpeakerTurn>& turns, double minDuration) {
    std::vector<Run> runs;
    for (size_t i = 0; i < words.size(); ++i) {
        const int speaker = speakerFor(words[i].start, words[i].end, turns);
        if (!runs.empty() && runs.back().speaker == speaker) {
            runs.back().last = i;
        } else {
            runs.push_back(Run{i, i, speaker});
        }
    }
    auto duration = [&](const Run& run) { return words[run.last].end - words[run.first].start; };
    while (runs.size() > 1) {
        auto shortest = std::min_element(runs.begin(), runs.end(), [&](const Run& a, const Run& b) { return duration(a) < duration(b); });
        if (duration(*shortest) >= minDuration) break;
        const size_t index = static_cast<size_t>(shortest - runs.begin());
        if (index > 0) {
            runs[index - 1].last = runs[index].last;
        } else {
            runs[1].first = runs[0].first;
        }
        runs.erase(runs.begin() + static_cast<std::ptrdiff_t>(index));
        for (size_t i = 1; i < runs.size();) {
            if (runs[i].speaker == runs[i - 1].speaker) {
                runs[i - 1].last = runs[i].last;
                runs.erase(runs.begin() + static_cast<std::ptrdiff_t>(i));
            } else {
                ++i;
            }
        }
    }
    return runs;
}

std::mutex liveDiarizersMutex;
std::atomic<bool> shuttingDown{false};
std::unordered_set<SpeakerDiarizer*>& liveDiarizers() {
    static auto* diarizers = new std::unordered_set<SpeakerDiarizer*>();
    return *diarizers;
}

void releaseLiveDiarizers() {
    shuttingDown.store(true);
    std::lock_guard lock(liveDiarizersMutex);
    for (auto* diarizer : liveDiarizers()) diarizer->release();
}

}

std::vector<Segment> SpeakerAssigner::assign(const std::vector<Segment>& segments, const std::vector<SpeakerTurn>& turns) const {
    if (turns.empty()) return segments;
    std::vector<Segment> result;
    result.reserve(segments.size());
    for (const auto& segment : segments) {
        if (!segment.words || segment.words->empty()) {
            Segment labeled = segment;
            labeled.speaker = std::to_string(speakerFor(segment.start, segment.end, turns));
            result.push_back(labeled);
            continue;
        }
        const auto& words = *segment.words;
        const auto runs = speakerRuns(words, turns, minRunDuration);
        if (runs.size() == 1) {
            Segment labeled = segment;
            labeled.speaker = std::to_string(runs.front().speaker);
            result.push_back(labeled);
            continue;
        }
        for (size_t i = 0; i < runs.size(); ++i) {
            Segment part = segment;
            if (i > 0) part.id = makeUUID();
            part.words = std::vector<WordTimestamp>(words.begin() + static_cast<std::ptrdiff_t>(runs[i].first),
                                                    words.begin() + static_cast<std::ptrdiff_t>(runs[i].last) + 1);
            part.start = i == 0 ? segment.start : words[runs[i].first].start;
            part.end = i + 1 == runs.size() ? segment.end : words[runs[i].last].end;
            part.text = joinWords(*part.words);
            part.speaker = std::to_string(runs[i].speaker);
            result.push_back(part);
        }
    }
    return result;
}

SpeakerDiarizer::SpeakerDiarizer(const std::filesystem::path& model, bool useGPU) {
    configureLogging();
    if (!std::filesystem::exists(model)) throw std::runtime_error("화자 구분 모델을 찾을 수 없습니다: " + model.filename().string());
    diarizer_ = nemo_speech::asr::Diarizer::load(useGPU ? 0 : -1, model.string(), nemo_speech::asr::DiarGeometry::preset("v3-offline"));
    std::lock_guard lock(liveDiarizersMutex);
    static std::once_flag once;
    std::call_once(once, [] { std::atexit(releaseLiveDiarizers); });
    liveDiarizers().insert(this);
}

SpeakerDiarizer::~SpeakerDiarizer() {
    {
        std::lock_guard lock(liveDiarizersMutex);
        liveDiarizers().erase(this);
    }
    release();
}

void SpeakerDiarizer::release() {
    std::lock_guard lock(mutex_);
    stream_.reset();
    diarizer_.reset();
}

std::vector<SpeakerTurn> SpeakerDiarizer::diarize(const AudioReader& read, double duration, const CancelToken& cancel) {
    auto stopped = [&] { return cancel.isCancelled() || shuttingDown.load(); };
    {
        std::lock_guard lock(mutex_);
        if (!diarizer_) throw Cancelled();
        stream_ = diarizer_->streaming_diarize();
    }
    struct StreamReset {
        SpeakerDiarizer& owner;
        ~StreamReset() {
            std::lock_guard lock(owner.mutex_);
            owner.stream_.reset();
        }
    } reset{*this};

    for (double start = 0; start < duration; start += windowSeconds) {
        if (stopped()) throw Cancelled();
        const auto samples = read(start, std::min(start + windowSeconds, duration));
        if (samples.empty()) break;
        std::lock_guard lock(mutex_);
        if (!stream_) throw Cancelled();
        stream_->push(samples.data(), samples.size(), kSampleRate);
    }

    std::lock_guard lock(mutex_);
    if (!stream_ || stopped()) throw Cancelled();
    stream_->finish();
    std::vector<SpeakerTurn> turns;
    for (const auto& segment : stream_->segments()) {
        turns.push_back(SpeakerTurn{segment.t0, segment.t1, segment.speaker + 1});
    }
    return turns;
}

}
