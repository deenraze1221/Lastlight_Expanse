#pragma once

#include <cstdlib>
#include <filesystem>

namespace zodar::test {

// UI resource directory for tests: $ZODAR_UI_DIR, else ./ui (a packaged build, e.g. on
// Windows), else the source tree path baked in at configure time.
inline std::filesystem::path uiTestDir() {
    if (const char* env = std::getenv("ZODAR_UI_DIR"); env && *env) {
        return env;
    }
    std::error_code ec;
    if (std::filesystem::exists("ui/MANIFEST.sha256", ec)) {
        return "ui";
    }
    return ZODAR_TEST_UI_DIR;
}

} // namespace zodar::test
