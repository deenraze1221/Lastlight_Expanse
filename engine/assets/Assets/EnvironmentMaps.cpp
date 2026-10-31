#include "Assets/EnvironmentMaps.h"

#include "Core/Error.h"

#include <cmath>
#include <vector>

namespace zodar {

ImageData proceduralSky(uint32_t width, const ProceduralSkyDesc& desc) {
    ZODAR_ENSURE(width >= 8 && width % 2 == 0, "proceduralSky: width must be even and >= 8");
    const uint32_t height = width / 2;
    const float pi = 3.14159265359f;
    const glm::vec3 toSun = -glm::normalize(desc.sunDirection);
    const float sunCos = std::cos(desc.sunAngularRadius);
    std::vector<float> rgba(size_t(width) * height * 4);
    for (uint32_t y = 0; y < height; ++y) {
        const float theta = (float(y) + 0.5f) / float(height) * pi; // 0 = up
        for (uint32_t x = 0; x < width; ++x) {
            const float phi = ((float(x) + 0.5f) / float(width) - 0.5f) * 2.0f * pi;
            const glm::vec3 d(std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi));
            glm::vec3 c;
            if (d.y >= 0.0f) {
                const float t = std::pow(1.0f - d.y, 3.0f);
                c = glm::mix(desc.zenithColor, desc.horizonColor, t) * desc.skyIntensity;
            } else {
                const float t = std::min(1.0f, -d.y * 4.0f);
                c = glm::mix(desc.horizonColor * 0.6f, desc.groundColor, t) * desc.skyIntensity;
            }
            const float cd = glm::dot(d, toSun);
            if (cd > sunCos) {
                c += desc.sunColor * desc.sunIntensity;
            } else {
                c += desc.sunColor * 0.08f * std::pow(std::max(cd, 0.0f), 64.0f) * desc.skyIntensity; // Glow
            }
            float* p = &rgba[(size_t(y) * width + x) * 4];
            p[0] = c.r;
            p[1] = c.g;
            p[2] = c.b;
            p[3] = 1.0f;
        }
    }
    return makeRgba16f(width, height, rgba, "ProceduralSky");
}

ImageData uniformEnvironment(const glm::vec3& radiance, uint32_t width) {
    const uint32_t height = std::max(1u, width / 2);
    std::vector<float> rgba(size_t(width) * height * 4);
    for (size_t i = 0; i < rgba.size(); i += 4) {
        rgba[i] = radiance.r;
        rgba[i + 1] = radiance.g;
        rgba[i + 2] = radiance.b;
        rgba[i + 3] = 1.0f;
    }
    return makeRgba16f(width, height, rgba, "UniformEnvironment");
}

} // namespace zodar
