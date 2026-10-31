#pragma once

// Run report: a machine-readable summary of one Application run, written as JSON at exit
// (--report). Agents read it instead of logs or the visual overlay.
//
// Layout (all times in milliseconds unless the key says seconds):
//   { "schema": "zodar.run_report", "version": 1, "application", "exitCode",
//     "run":    { frames, fixedSteps, wallSeconds, simulatedSeconds, fixedDt, headless,
//                 deterministic, width, height, seed|null },
//     "device": { name, driver, apiVersion, softwareRasterizer, validation },
//     "validation": { errors, warnings }, "log": { warnings, errors },
//     "fps":    { mean, p50, p95, p99 }   // p95/p99: FPS at the 95th/99th percentile frame time
//     "frameMs" | "cpuFrameMs" | "gpuFrameMs": { count, mean, min, max, p50, p95, p99 } | null,
//     "cpuScopes" | "gpuPasses": { "<name>": { count, mean, ... } },
//     "render": { lastFrame RenderStats }, "memory": { last + peak device-local usage },
//     "screenshots": [ { path, frame, ok } ] }

#include "Renderer/GpuProfiler.h"
#include "Renderer/SceneRenderer.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace zodar {

struct SampleSummary {
    uint64_t count = 0;
    double mean = 0.0;
    double min = 0.0;
    double max = 0.0;
    double p50 = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
};

// Nearest-rank percentiles; all zero for no samples.
SampleSummary summarize(std::vector<double> samples);

// Per-name sample series in first-seen order (CPU scopes, GPU passes).
class NamedSeries {
public:
    void add(const std::string& name, double value);
    const std::vector<std::pair<std::string, std::vector<double>>>& series() const { return m_series; }

private:
    std::vector<std::pair<std::string, std::vector<double>>> m_series;
};

struct RunReport {
    struct Capture {
        std::string path;
        uint64_t frame = 0; // 1-based
        bool ok = false;
    };

    std::string application;
    int exitCode = 0;

    uint64_t frames = 0;
    uint64_t fixedSteps = 0;
    double wallSeconds = 0.0;
    double simulatedSeconds = 0.0;
    double fixedDt = 0.0;
    bool headless = false;
    bool deterministic = false;
    uint32_t width = 0;
    uint32_t height = 0;
    std::optional<uint64_t> seed;

    std::string deviceName;
    std::string driver;
    std::string apiVersion;
    bool softwareRasterizer = false;
    bool validation = false;
    uint32_t validationErrors = 0;
    uint32_t validationWarnings = 0;
    uint64_t logWarnings = 0;
    uint64_t logErrors = 0;

    std::vector<double> frameMs;    // wall-clock interval per rendered frame
    std::vector<double> cpuFrameMs; // CPU profiler frame time
    std::vector<double> gpuFrameMs; // GPU timestamps (empty when unavailable)
    NamedSeries cpuScopes;
    NamedSeries gpuPasses;

    gfx::RenderStats render;
    gfx::GpuMemoryStats memory;
    uint64_t peakDeviceLocalUsage = 0;

    std::vector<Capture> screenshots;
};

std::string toJson(const RunReport& report);
// Returns false if the file cannot be written.
bool writeRunReport(const std::filesystem::path& path, const RunReport& report);

} // namespace zodar
