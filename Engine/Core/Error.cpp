#include "Core/Error.h"

#include "Core/Log.h"

#include <filesystem>

namespace raven {

void fail(std::string_view message, std::source_location location) {
    const auto file = std::filesystem::path(location.file_name()).filename().string();
    auto text = fmt::format("{} ({}:{})", message, file, location.line());
    RAVEN_LOG_ERROR("{}", text);
    throw Error(text);
}

} // namespace raven
