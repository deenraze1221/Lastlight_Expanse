#pragma once
#include "Input/Input.h"
#include <cstdint>

namespace zodar {
struct RecordingHeader {
    std::string app, inputMapHash, levelHash;
    uint64_t seed = 0;
    double fixedDt = 1.0/60.0;
    bool operator==(const RecordingHeader&) const = default;
};
struct RecordedTick {
    ActionSnapshot actions;
    std::string state; // app fingerprint + named RNG states, after simulation of this tick
};
// Complete recordings have a checked footer. Parsing validates every tick before playback.
// In-memory snapshots favor simple deterministic playback; the on-disk JSONL is delta encoded.
struct InputRecording {
    RecordingHeader header;
    std::vector<RecordedTick> ticks;
    std::string serialize() const;
    static InputRecording parse(std::string_view bytes, std::string_view file = "<recording>");
    static InputRecording load(const std::filesystem::path& file);
    void save(const std::filesystem::path& file) const;
    void checkCompatible(const RecordingHeader& expected, bool lenientHashes = false) const;
};
} // namespace zodar
