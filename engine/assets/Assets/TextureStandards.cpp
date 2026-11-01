#include "Assets/TextureStandards.h"

#include "Core/Error.h"

#include <algorithm>
#include <bit>

namespace zodar {

TextureSizeClass classifyTextureSize(uint32_t width, uint32_t height) {
    if (width != height || !std::has_single_bit(width) || width < kTextureSizeMin) {
        return TextureSizeClass::NonStandard;
    }
    if (width < kTextureSizeDefault) return TextureSizeClass::Small;
    if (width == kTextureSizeDefault) return TextureSizeClass::Default;
    if (width == kTextureSizeMax) return TextureSizeClass::Maximum;
    return TextureSizeClass::Oversized;
}

const char* toString(TextureSizeClass sizeClass) {
    switch (sizeClass) {
    case TextureSizeClass::Small: return "small (below 32x32)";
    case TextureSizeClass::Default: return "default (32x32)";
    case TextureSizeClass::Maximum: return "maximum (64x64)";
    case TextureSizeClass::Oversized: return "oversized (above 64x64)";
    case TextureSizeClass::NonStandard: break;
    }
    return "non-standard";
}

bool isTextureSizeAllowed(TextureSizeClass sizeClass) {
    return sizeClass == TextureSizeClass::Small || sizeClass == TextureSizeClass::Default ||
           sizeClass == TextureSizeClass::Maximum;
}

namespace {

std::string sizeText(uint32_t width, uint32_t height) { return std::to_string(width) + "x" + std::to_string(height); }

std::string sizeList(const std::vector<uint32_t>& sizes) {
    std::string s;
    for (size_t i = 0; i < sizes.size(); ++i) {
        if (i) s += ", ";
        s += sizeText(sizes[i], sizes[i]);
    }
    return s;
}

} // namespace

TextureStandard TextureStandard::engineDefault() {
    TextureStandard s;
    s.classes = {{"default", {8, 16, kTextureSizeDefault, kTextureSizeMax}}};
    s.defaultClass = "default";
    s.engineRule = true;
    return s;
}

const TextureClassRule* TextureStandard::find(std::string_view name) const {
    for (const TextureClassRule& c : classes) {
        if (c.name == name) return &c;
    }
    return nullptr;
}

void TextureStandard::validate() const {
    if (classes.empty()) fail("texture standard: no texture classes");
    if (prohibitedSize == 0 || prohibitedSize > kTextureSizeProhibited) {
        fail("texture standard: prohibitedSize " + std::to_string(prohibitedSize) + " must be in 1..256");
    }
    for (size_t i = 0; i < classes.size(); ++i) {
        const TextureClassRule& c = classes[i];
        const std::string where = "texture standard: class '" + c.name + "'";
        if (c.name.empty()) fail("texture standard: class " + std::to_string(i) + " has an empty name");
        for (size_t j = 0; j < i; ++j) {
            if (classes[j].name == c.name) fail(where + " is declared twice");
        }
        if (c.sizes.empty()) fail(where + " allows no sizes");
        for (size_t k = 0; k < c.sizes.size(); ++k) {
            const uint32_t size = c.sizes[k];
            if (!std::has_single_bit(size)) fail(where + ": size " + std::to_string(size) + " is not a power of two");
            if (size >= prohibitedSize) {
                fail(where + ": size " + std::to_string(size) + " is at or above the prohibited size " +
                     std::to_string(prohibitedSize));
            }
            if (k > 0 && size <= c.sizes[k - 1]) fail(where + ": sizes must be ascending and unique");
        }
    }
    if (!find(defaultClass)) fail("texture standard: default class '" + defaultClass + "' is not declared");
    if (!(densityTarget > 0.0)) fail("texture standard: densityTarget must be positive");
}

std::optional<TextureSizeIssue> checkTextureSize(const TextureStandard& standard, std::string_view name,
                                                 uint32_t width, uint32_t height, std::string_view textureClass) {
    const std::string size = sizeText(width, height);
    if (width >= standard.prohibitedSize || height >= standard.prohibitedSize) {
        return TextureSizeIssue{TextureSizeVerdict::Prohibited,
                                std::string(name) + ": " + size + " is prohibited (" +
                                    sizeText(standard.prohibitedSize, standard.prohibitedSize) +
                                    " and above need an approved exception; not resized: re-author the texture)"};
    }
    if (standard.engineRule && textureClass.empty()) {
        if (auto legacy = checkTextureSize(name, width, height)) {
            return TextureSizeIssue{TextureSizeVerdict::Violation, std::move(*legacy)};
        }
        return std::nullopt;
    }
    const std::string_view className = textureClass.empty() ? std::string_view(standard.defaultClass) : textureClass;
    const TextureClassRule* rule = standard.find(className);
    if (!rule) {
        return TextureSizeIssue{TextureSizeVerdict::Violation,
                                std::string(name) + ": unknown texture class '" + std::string(className) + "'"};
    }
    if (width == height) {
        for (uint32_t allowed : rule->sizes) {
            if (allowed == width) return std::nullopt;
        }
    }
    return TextureSizeIssue{TextureSizeVerdict::Violation,
                            std::string(name) + ": " + size + " is not allowed for texture class '" + rule->name +
                                "' (" + sizeList(rule->sizes) + "; not resized: re-author the texture)"};
}

std::optional<TextureSizeIssue> checkTextureSizeAnyClass(const TextureStandard& standard, std::string_view name,
                                                         uint32_t width, uint32_t height) {
    if (standard.engineRule) return checkTextureSize(standard, name, width, height);
    if (auto issue = checkTextureSize(standard, name, width, height, standard.defaultClass)) {
        if (issue->verdict == TextureSizeVerdict::Prohibited) return issue;
    } else {
        return std::nullopt;
    }
    std::vector<uint32_t> all;
    for (const TextureClassRule& c : standard.classes) {
        for (uint32_t size : c.sizes) {
            if (size == width && width == height) return std::nullopt;
            if (std::find(all.begin(), all.end(), size) == all.end()) all.push_back(size);
        }
    }
    std::sort(all.begin(), all.end());
    return TextureSizeIssue{TextureSizeVerdict::Violation, std::string(name) + ": " + sizeText(width, height) +
                                                               " is not allowed by any texture class (" +
                                                               sizeList(all) + "; not resized: re-author the texture)"};
}

std::optional<std::string> checkTextureSize(std::string_view name, uint32_t width, uint32_t height) {
    const TextureSizeClass c = classifyTextureSize(width, height);
    if (isTextureSizeAllowed(c)) {
        return std::nullopt;
    }
    const std::string size = sizeText(width, height);
    if (width >= kTextureSizeProhibited || height >= kTextureSizeProhibited) {
        return std::string(name) + ": " + size +
               " is prohibited (256x256 and above need an approved exception; not resized: re-author the texture)";
    }
    if (c == TextureSizeClass::Oversized) {
        return std::string(name) + ": " + size +
               " exceeds the 64x64 maximum texture size (default 32x32; not resized: re-author the texture)";
    }
    return std::string(name) + ": " + size +
           " is not a standard texture size (square power of two: 32x32 default, 64x64 maximum, 8/16 for tiny "
           "details)";
}

} // namespace zodar
