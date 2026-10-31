#include <doctest/doctest.h>

#include "Runtime/RunReport.h"

#include <string>

using zodar::RunReport;
using zodar::summarize;

TEST_CASE("RunReport summarize uses nearest-rank percentiles") {
    std::vector<double> samples;
    for (int i = 100; i >= 1; --i) samples.push_back(i); // unsorted input
    const auto s = summarize(samples);
    CHECK(s.count == 100);
    CHECK(s.mean == doctest::Approx(50.5));
    CHECK(s.min == 1.0);
    CHECK(s.max == 100.0);
    CHECK(s.p50 == 50.0);
    CHECK(s.p95 == 95.0);
    CHECK(s.p99 == 99.0);

    CHECK(summarize({7.0}).p99 == 7.0);
    CHECK(summarize({}).count == 0);
}

TEST_CASE("RunReport NamedSeries keeps first-seen order") {
    zodar::NamedSeries series;
    series.add("Update", 1.0);
    series.add("Render", 2.0);
    series.add("Update", 3.0);
    REQUIRE(series.series().size() == 2);
    CHECK(series.series()[0].first == "Update");
    CHECK(series.series()[0].second.size() == 2);
    CHECK(series.series()[1].first == "Render");
}

TEST_CASE("RunReport JSON has the documented keys and escapes strings") {
    RunReport r;
    r.application = "Test \"App\"\n";
    r.exitCode = 3;
    r.frames = 4;
    r.seed = 7;
    r.frameMs = {10.0, 20.0, 10.0};
    r.cpuFrameMs = {1.0, 2.0};
    r.cpuScopes.add("Update", 0.5);
    r.screenshots.push_back({"C:\\shots\\a.png", 4, true});
    const std::string j = zodar::toJson(r);
    for (const char* key : {"\"schema\": \"zodar.run_report\"", "\"version\": 1", "\"exitCode\": 3", "\"frames\": 4",
                            "\"seed\": 7", "\"fps\":", "\"frameMs\": {\"count\": 3", "\"gpuFrameMs\": null",
                            "\"cpuScopes\": {", "\"Update\": {\"count\": 1", "\"gpuPasses\": {}", "\"render\":",
                            "\"memory\":", "\"validation\":", "\"log\":"}) {
        CHECK_MESSAGE(j.find(key) != std::string::npos, key);
    }
    CHECK(j.find("\"application\": \"Test \\\"App\\\"\\n\"") != std::string::npos);
    CHECK(j.find("\"path\": \"C:\\\\shots\\\\a.png\", \"frame\": 4, \"ok\": true") != std::string::npos);
    // fps p50 from the 10 ms median frame time.
    CHECK(j.find("\"p50\": 100.0000") != std::string::npos);

    RunReport empty;
    const std::string e = zodar::toJson(empty);
    CHECK(e.find("\"seed\": null") != std::string::npos);
    CHECK(e.find("\"frameMs\": null") != std::string::npos);
    CHECK(e.find("\"screenshots\": []") != std::string::npos);
}
