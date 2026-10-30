#pragma once

#include "UI/Geometry.h"
#include "UI/TextureStore.h"

#include <filesystem>
#include <optional>
#include <string>

struct NSVGimage;

namespace zodar::ui {

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

private:
    explicit SvgDocument(NSVGimage* image) : m_image(image) {}
    NSVGimage* m_image = nullptr;
};

} // namespace zodar::ui
