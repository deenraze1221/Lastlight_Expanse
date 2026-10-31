#pragma once

#include "UI/Geometry.h"
#include "UI/TextureStore.h"
#include "UI/Theme.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct NSVGimage;

namespace zodar::ui {

// One drawable shape of a parsed SVG, in document order (for selecting shapes to draw).
struct SvgShapeInfo {
    size_t index = 0;
    std::string id;
    bool hasFill = false;
    Color fill;          // Includes fill-opacity in alpha.
    bool hasStroke = false;
    Color stroke;
    float strokeWidth = 0.0f;
    Rect bounds;         // SVG units.
};
// Returns true to draw the shape.
using SvgShapeFilter = std::function<bool(const SvgShapeInfo&)>;

// nanosvg has no <mask> support and would draw a mask's contents as ordinary shapes, so
// <mask>...</mask> definitions are removed from the in-memory text before parsing (the file on
// disk is never modified). Masked elements are then drawn unmasked. Returns the number removed.
size_t stripMaskDefinitions(std::string& svgText);

// A parsed SVG document (the original brand files are used unmodified). Rasterized to
// CPU images at whatever scale the UI needs, so logos stay sharp at every DPI.
class SvgDocument {
public:
    static SvgDocument load(const std::filesystem::path& path); // Fails loudly.
    static SvgDocument parse(std::string text);                 // Fails loudly.

    SvgDocument(SvgDocument&& other) noexcept;
    SvgDocument& operator=(SvgDocument&& other) noexcept;
    ~SvgDocument();
    SvgDocument(const SvgDocument&) = delete;
    SvgDocument& operator=(const SvgDocument&) = delete;

    // Document size in SVG user units (width/height attributes).
    Size2 size() const;
    // Rasterizes `region` (SVG units; default: whole document) at `scale` pixels per unit.
    Image rasterize(float scale, std::optional<Rect> region = std::nullopt) const;
    // Same, drawing only the shapes `keep` accepts. Not thread-safe for one document.
    Image rasterize(float scale, std::optional<Rect> region, const SvgShapeFilter& keep) const;

    std::vector<SvgShapeInfo> shapes() const;
    // <mask> definitions removed while parsing (see stripMaskDefinitions).
    size_t strippedMasks() const { return m_strippedMasks; }

private:
    explicit SvgDocument(NSVGimage* image, size_t strippedMasks = 0) : m_image(image), m_strippedMasks(strippedMasks) {}
    NSVGimage* m_image = nullptr;
    size_t m_strippedMasks = 0;
};

} // namespace zodar::ui
