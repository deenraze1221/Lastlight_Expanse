#include "UI/Svg.h"

#include "Core/Error.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

#define NANOSVG_IMPLEMENTATION
#include <nanosvg.h>
#define NANOSVGRAST_IMPLEMENTATION
#include <nanosvgrast.h>

namespace zodar::ui {

SvgDocument SvgDocument::load(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        fail("SVG not found: " + path.string());
    }
    return parse(std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()));
}

SvgDocument SvgDocument::parse(std::string text) {
    // nanosvg parses in place; it gets its own mutable copy, the source stays untouched.
    std::vector<char> buffer(text.begin(), text.end());
    buffer.push_back('\0');
    NSVGimage* image = nsvgParse(buffer.data(), "px", 96.0f);
    if (!image || image->width <= 0.0f || image->height <= 0.0f) {
        if (image) nsvgDelete(image);
        fail("SVG could not be parsed");
    }
    return SvgDocument(image);
}

SvgDocument::SvgDocument(SvgDocument&& other) noexcept : m_image(other.m_image) {
    other.m_image = nullptr;
}

SvgDocument& SvgDocument::operator=(SvgDocument&& other) noexcept {
    if (this != &other) {
        if (m_image) nsvgDelete(m_image);
        m_image = other.m_image;
        other.m_image = nullptr;
    }
    return *this;
}

SvgDocument::~SvgDocument() {
    if (m_image) {
        nsvgDelete(m_image);
    }
}

Size2 SvgDocument::size() const {
    return {m_image->width, m_image->height};
}

Image SvgDocument::rasterize(float scale, std::optional<Rect> region) const {
    ZODAR_ENSURE(scale > 0.0f, "SVG scale must be positive");
    const Rect r = region.value_or(Rect{0, 0, m_image->width, m_image->height});
    Image out;
    out.width = static_cast<uint32_t>(std::max(1.0f, std::ceil(r.width * scale)));
    out.height = static_cast<uint32_t>(std::max(1.0f, std::ceil(r.height * scale)));
    out.pixels.assign(size_t{out.width} * out.height * 4, 0);
    NSVGrasterizer* rast = nsvgCreateRasterizer();
    ZODAR_ENSURE(rast != nullptr, "nanosvg rasterizer allocation failed");
    nsvgRasterize(rast, m_image, -r.x * scale, -r.y * scale, scale, out.pixels.data(), static_cast<int>(out.width),
                  static_cast<int>(out.height), static_cast<int>(out.width * 4));
    nsvgDeleteRasterizer(rast);
    return out;
}

} // namespace zodar::ui
