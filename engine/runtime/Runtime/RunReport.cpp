#include "Runtime/RunReport.h"

#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <numeric>

namespace zodar {

namespace {

std::string escape(std::string_view in) {
    std::string out;
    out.reserve(in.size() + 2);
    out += '"';
    for (const char c : in) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) out += fmt::format("\\u{:04x}", static_cast<unsigned>(c));
            else out += c;
        }
    }
    out += '"';
    return out;
}

std::string number(double v) {
    if (!std::isfinite(v)) return "null";
    return fmt::format("{:.4f}", v);
}

std::string boolean(bool v) {
    return v ? "true" : "false";
}

std::string summary(const SampleSummary& s) {
    return fmt::format(R"({{"count": {}, "mean": {}, "min": {}, "max": {}, "p50": {}, "p95": {}, "p99": {}}})", s.count,
                       number(s.mean), number(s.min), number(s.max), number(s.p50), number(s.p95), number(s.p99));
}

std::string optionalSummary(const std::vector<double>& samples) {
    return samples.empty() ? "null" : summary(summarize(samples));
}

std::string series(const NamedSeries& named, const char* indent) {
    if (named.series().empty()) return "{}";
    std::string out = "{";
    bool first = true;
    for (const auto& [name, values] : named.series()) {
        out += first ? "\n" : ",\n";
        out += fmt::format("{}  {}: {}", indent, escape(name), summary(summarize(values)));
        first = false;
    }
    return out + "\n" + indent + "}";
}

double fps(double ms) {
    return ms > 0.0 ? 1000.0 / ms : 0.0;
}

} // namespace

SampleSummary summarize(std::vector<double> samples) {
    SampleSummary s;
    if (samples.empty()) return s;
    std::sort(samples.begin(), samples.end());
    const auto rank = [&](double fraction) {
        // Nearest rank: smallest value with at least `fraction` of samples at or below it.
        const auto n = static_cast<double>(samples.size());
        const auto index = static_cast<size_t>(std::max(1.0, std::ceil(fraction * n))) - 1;
        return samples[std::min(index, samples.size() - 1)];
    };
    s.count = samples.size();
    s.mean = std::accumulate(samples.begin(), samples.end(), 0.0) / static_cast<double>(samples.size());
    s.min = samples.front();
    s.max = samples.back();
    s.p50 = rank(0.50);
    s.p95 = rank(0.95);
    s.p99 = rank(0.99);
    return s;
}

void NamedSeries::add(const std::string& name, double value) {
    for (auto& [existing, values] : m_series) {
        if (existing == name) {
            values.push_back(value);
            return;
        }
    }
    m_series.emplace_back(name, std::vector<double>{value});
}

std::string toJson(const RunReport& r) {
    const SampleSummary frame = summarize(r.frameMs);
    const gfx::RenderStats& rs = r.render;
    std::string screenshots = "[";
    for (size_t i = 0; i < r.screenshots.size(); ++i) {
        const RunReport::Capture& c = r.screenshots[i];
        screenshots += fmt::format(R"({}{{"path": {}, "frame": {}, "ok": {}}})", i ? ", " : "", escape(c.path), c.frame,
                                   boolean(c.ok));
    }
    screenshots += "]";

    std::string j = "{\n";
    j += "  \"schema\": \"zodar.run_report\",\n  \"version\": 1,\n";
    j += fmt::format("  \"application\": {},\n  \"exitCode\": {},\n", escape(r.application), r.exitCode);
    j += fmt::format("  \"run\": {{\"frames\": {}, \"fixedSteps\": {}, \"wallSeconds\": {}, \"simulatedSeconds\": {}, "
                     "\"fixedDt\": {}, \"headless\": {}, \"deterministic\": {}, \"width\": {}, \"height\": {}, "
                     "\"seed\": {}}},\n",
                     r.frames, r.fixedSteps, number(r.wallSeconds), number(r.simulatedSeconds), number(r.fixedDt),
                     boolean(r.headless), boolean(r.deterministic), r.width, r.height,
                     r.seed ? std::to_string(*r.seed) : "null");
    j += fmt::format("  \"device\": {{\"name\": {}, \"driver\": {}, \"apiVersion\": {}, \"softwareRasterizer\": {}, "
                     "\"validation\": {}}},\n",
                     escape(r.deviceName), escape(r.driver), escape(r.apiVersion), boolean(r.softwareRasterizer),
                     boolean(r.validation));
    j += fmt::format("  \"validation\": {{\"errors\": {}, \"warnings\": {}}},\n", r.validationErrors,
                     r.validationWarnings);
    j += fmt::format("  \"log\": {{\"warnings\": {}, \"errors\": {}}},\n", r.logWarnings, r.logErrors);
    j += fmt::format("  \"fps\": {{\"mean\": {}, \"p50\": {}, \"p95\": {}, \"p99\": {}}},\n",
                     number(frame.count ? fps(frame.mean) : 0.0), number(fps(frame.p50)), number(fps(frame.p95)),
                     number(fps(frame.p99)));
    j += fmt::format("  \"frameMs\": {},\n  \"cpuFrameMs\": {},\n  \"gpuFrameMs\": {},\n", optionalSummary(r.frameMs),
                     optionalSummary(r.cpuFrameMs), optionalSummary(r.gpuFrameMs));
    j += fmt::format("  \"cpuScopes\": {},\n  \"gpuPasses\": {},\n", series(r.cpuScopes, "  "),
                     series(r.gpuPasses, "  "));
    j += fmt::format("  \"render\": {{\"submitted\": {}, \"culled\": {}, \"drawCalls\": {}, \"instances\": {}, "
                     "\"triangles\": {}, \"commandDraws\": {}, \"lights\": {}}},\n",
                     rs.submitted, rs.culled, rs.drawCalls, rs.instances, rs.triangles, rs.commandDraws, rs.lights);
    j += fmt::format("  \"memory\": {{\"deviceLocalUsage\": {}, \"deviceLocalBudget\": {}, \"allocationBytes\": {}, "
                     "\"peakDeviceLocalUsage\": {}}},\n",
                     r.memory.deviceLocalUsage, r.memory.deviceLocalBudget, r.memory.allocationBytes,
                     r.peakDeviceLocalUsage);
    j += fmt::format("  \"screenshots\": {}\n}}\n", screenshots);
    return j;
}

bool writeRunReport(const std::filesystem::path& path, const RunReport& report) {
    if (path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    const std::string json = toJson(report);
    file.write(json.data(), static_cast<std::streamsize>(json.size()));
    return static_cast<bool>(file);
}

} // namespace zodar
