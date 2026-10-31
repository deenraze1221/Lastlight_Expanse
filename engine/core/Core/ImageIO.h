#pragma once

#include <cstdint>
#include <filesystem>
#include <span>

namespace zodar {

// Writes tightly packed 8-bit RGBA pixels as a binary PPM (alpha is dropped).
// PPM keeps Milestone 1 free of an image-encoding dependency; it is used for
// renderer screenshots and is viewable or convertible with standard tools.
// Returns false if the size does not match or the file cannot be written.
bool writePpm(const std::filesystem::path& path, uint32_t width, uint32_t height,
              std::span<const uint8_t> rgba);

} // namespace zodar
