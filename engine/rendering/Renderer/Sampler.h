#pragma once

#include <volk.h>

#include <cstddef>
#include <functional>

namespace zodar::gfx {

enum class Filter { Nearest, Linear };
enum class AddressMode { Repeat, MirroredRepeat, ClampToEdge };

// Style-neutral sampler description. Point sampling, trilinear and anisotropic
// filtering are all equal options; the material or game chooses.
struct SamplerDesc {
    Filter magFilter = Filter::Linear;
    Filter minFilter = Filter::Linear;
    Filter mipFilter = Filter::Linear;
    AddressMode addressU = AddressMode::Repeat;
    AddressMode addressV = AddressMode::Repeat;
    float maxAnisotropy = 16.0f; // <= 1 disables anisotropic filtering; clamped to device limit.
    float mipLodBias = 0.0f;
    bool enableMips = true;      // false: sample mip 0 only.

    bool operator==(const SamplerDesc&) const = default;

    static SamplerDesc linear() { return {}; }
    static SamplerDesc nearest() {
        SamplerDesc d;
        d.magFilter = d.minFilter = d.mipFilter = Filter::Nearest;
        d.maxAnisotropy = 1.0f;
        return d;
    }
    // Pixel art (docs/ART_DIRECTION.md): nearest magnification and minification keep art
    // pixels crisp; linear blending between mip levels stops distant shimmer.
    static SamplerDesc pixelArt() {
        SamplerDesc d = nearest();
        d.mipFilter = Filter::Linear;
        return d;
    }
};

struct SamplerDescHash {
    size_t operator()(const SamplerDesc& d) const noexcept;
};

} // namespace zodar::gfx
