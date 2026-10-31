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

size_t stripMaskDefinitions(std::string& text) {
    size_t removed = 0;
    size_t pos = 0;
    while ((pos = text.find("<mask", pos)) != std::string::npos) {
        const char after = pos + 5 < text.size() ? text[pos + 5] : '\0';
        if (after != ' ' && after != '>' && after != '/' && after != '\t' && after != '\n' && after != '\r') {
            pos += 5; // e.g. "<maskFoo": not a mask element.
            continue;
        }
        const size_t tagEnd = text.find('>', pos);
        if (tagEnd == std::string::npos) break;
        size_t end = tagEnd + 1;
        if (text[tagEnd - 1] != '/') { // Not self-closing: remove through </mask>.
            const size_t close = text.find("</mask>", tagEnd);
            if (close == std::string::npos) break;
            end = close + 7;
        }
        text.erase(pos, end - pos);
        ++removed;
    }
    return removed;
}

SvgDocument SvgDocument::parse(std::string text) {
    const size_t masks = stripMaskDefinitions(text);
    // nanosvg parses in place; it gets its own mutable copy, the source stays untouched.
    std::vector<char> buffer(text.begin(), text.end());
    buffer.push_back('\0');
    NSVGimage* image = nsvgParse(buffer.data(), "px", 96.0f);
    if (!image || image->width <= 0.0f || image->height <= 0.0f) {
        if (image) nsvgDelete(image);
        fail("SVG could not be parsed");
    }
    return SvgDocument(image, masks);
}

SvgDocument::SvgDocument(SvgDocument&& other) noexcept : m_image(other.m_image), m_strippedMasks(other.m_strippedMasks) {
    other.m_image = nullptr;
}

SvgDocument& SvgDocument::operator=(SvgDocument&& other) noexcept {
    if (this != &other) {
        if (m_image) nsvgDelete(m_image);
        m_image = other.m_image;
        m_strippedMasks = other.m_strippedMasks;
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

namespace {
// nanosvg packs colours as 0xAABBGGRR.
Color fromNsvg(unsigned int c) {
    return {static_cast<uint8_t>(c & 0xFF), static_cast<uint8_t>((c >> 8) & 0xFF), static_cast<uint8_t>((c >> 16) & 0xFF),
            static_cast<uint8_t>((c >> 24) & 0xFF)};
}
} // namespace

std::vector<SvgShapeInfo> SvgDocument::shapes() const {
    std::vector<SvgShapeInfo> out;
    size_t index = 0;
    for (NSVGshape* s = m_image->shapes; s; s = s->next, ++index) {
        SvgShapeInfo info;
        info.index = index;
        info.id = s->id;
        info.hasFill = s->fill.type != NSVG_PAINT_NONE;
        info.fill = info.hasFill ? fromNsvg(s->fill.color) : Color{0, 0, 0, 0};
        info.hasStroke = s->stroke.type != NSVG_PAINT_NONE;
        info.stroke = info.hasStroke ? fromNsvg(s->stroke.color) : Color{0, 0, 0, 0};
        info.strokeWidth = s->strokeWidth;
        info.bounds = {s->bounds[0], s->bounds[1], s->bounds[2] - s->bounds[0], s->bounds[3] - s->bounds[1]};
        out.push_back(std::move(info));
    }
    return out;
}

Image SvgDocument::rasterize(float scale, std::optional<Rect> region, const SvgShapeFilter& keep) const {
    if (!keep) {
        return rasterize(scale, region);
    }
    // Temporarily hide rejected shapes (nanosvg skips shapes without NSVG_FLAGS_VISIBLE).
    const std::vector<SvgShapeInfo> infos = shapes();
    std::vector<NSVGshape*> hidden;
    size_t index = 0;
    for (NSVGshape* s = m_image->shapes; s; s = s->next, ++index) {
        if ((s->flags & NSVG_FLAGS_VISIBLE) && !keep(infos[index])) {
            s->flags = static_cast<unsigned char>(s->flags & ~NSVG_FLAGS_VISIBLE);
            hidden.push_back(s);
        }
    }
    struct Restore {
        std::vector<NSVGshape*>& shapes;
        ~Restore() {
            for (NSVGshape* s : shapes) s->flags = static_cast<unsigned char>(s->flags | NSVG_FLAGS_VISIBLE);
        }
    } restore{hidden};
    return rasterize(scale, region);
}

} // namespace zodar::ui
