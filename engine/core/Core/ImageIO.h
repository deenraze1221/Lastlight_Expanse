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

// Same input and contract as writePpm, written as an 8-bit RGB PNG (stb_image_write; alpha is
// dropped). PNG is the format agents and image viewers read directly.
bool writePng(const std::filesystem::path& path, uint32_t width, uint32_t height,
              std::span<const uint8_t> rgba);

// Picks the format from the extension (any case): ".png" writes PNG, ".ppm" PPM. Any other
// extension is rejected (returns false, nothing written).
bool writeImage(const std::filesystem::path& path, uint32_t width, uint32_t height,
                std::span<const uint8_t> rgba);

} // namespace zodar
