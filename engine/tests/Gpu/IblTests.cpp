#include "GpuTestContext.h"

#include "Assets/EnvironmentMaps.h"
#include "Assets/Primitives.h"
#include "Renderer/Mesh.h"
#include "Renderer/Resources/FrameAllocator.h"
#include "Renderer/SceneRenderer.h"

#include <glm/gtc/packing.hpp>

#include <cmath>
#include <vector>

using namespace zodar;
using namespace zodar::gfx;
using zodar::test::gpu;

namespace {

std::vector<glm::vec4> toFloats(const std::vector<uint8_t>& bytes) {
    std::vector<glm::vec4> out(bytes.size() / 8);
    const auto* h = reinterpret_cast<const uint16_t*>(bytes.data());
    for (size_t i = 0; i < out.size(); ++i) {
        out[i] = {glm::unpackHalf1x16(h[i * 4]), glm::unpackHalf1x16(h[i * 4 + 1]), glm::unpackHalf1x16(h[i * 4 + 2]),
                  glm::unpackHalf1x16(h[i * 4 + 3])};
    }
    return out;
}

// CPU reference of the DFG LUT texel (same estimator as ibl_dfg.comp, more samples).
glm::vec2 referenceDfg(float nDotV, float roughness, uint32_t samples) {
    const float pi = 3.14159265359f;
    const float alpha = std::max(roughness * roughness, 0.002f);
    const glm::vec3 v(std::sqrt(1.0f - nDotV * nDotV), 0.0f, nDotV);
    glm::vec2 r(0.0f);
    for (uint32_t i = 0; i < samples; ++i) {
        const float u1 = (float(i) + 0.5f) / float(samples);
        const float u2 = std::fmod(float(i) * 0.6180339887f, 1.0f);
        const float phi = 2.0f * pi * u1;
        const float cosTheta = std::sqrt((1.0f - u2) / (1.0f + (alpha * alpha - 1.0f) * u2));
        const float sinTheta = std::sqrt(1.0f - cosTheta * cosTheta);
        const glm::vec3 h(sinTheta * std::cos(phi), sinTheta * std::sin(phi), cosTheta);
        const glm::vec3 l = 2.0f * glm::dot(v, h) * h - v;
        const float nDotL = std::clamp(l.z, 0.0f, 1.0f), nDotH = std::clamp(h.z, 0.0f, 1.0f);
        const float vDotH = std::clamp(glm::dot(v, h), 0.0f, 1.0f);
        if (nDotL <= 0.0f) continue;
        const float a2 = alpha * alpha;
        const float vis = 0.5f / (nDotL * std::sqrt(nDotV * nDotV * (1 - a2) + a2) + nDotV * std::sqrt(nDotL * nDotL * (1 - a2) + a2));
        const float gv = vis * 4.0f * nDotL * vDotH / std::max(nDotH, 1e-6f);
        const float fc = std::pow(1.0f - vDotH, 5.0f);
        r += glm::vec2(gv * fc, gv);
    }
    return r / float(samples);
}

struct Fixture {
    SceneRenderer scene{*gpu().context, *gpu().upload, {zodar::test::shaderDir()}};
    FrameAllocator scratch{*gpu().context, 2};
    GpuMesh sphere;
    uint64_t frame = 0;

    Fixture() {
        UploadBatch batch(*gpu().upload);
        sphere = GpuMesh(batch, primitives::uvSphere(1.0f, 64, 32));
        batch.submit();
    }

    std::vector<glm::vec4> render(const SceneView& view, VkExtent2D extent) {
        scratch.beginFrame(uint32_t(frame % 2));
        gpu().upload->immediate([&](VkCommandBuffer cmd) {
            scene.render({cmd, uint32_t(frame % 2), frame, &scratch, nullptr}, extent, view);
        });
        ++frame;
        return toFloats(gpu().upload->readImage(scene.colorTarget(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
    }
};

SceneView sphereView() {
    SceneView view;
    view.camera.position = {0.0f, 0.0f, 3.2f};
    view.camera.lookAt({0.0f, 0.0f, 0.0f});
    view.lighting.ambientIntensity = 0.0f;
    view.lighting.sun.enabled = false;
    return view;
}

} // namespace

TEST_CASE("ibl: DFG LUT matches a CPU reference integration") {
    zodar::test::ValidationGuard guard;
    Fixture f;
    const auto lut = toFloats(gpu().upload->readImage(f.scene.ibl().dfgLut(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
    const uint32_t n = IblBaker::kDfgLutSize;
    for (uint32_t y : {0u, 20u, 64u, 100u, 127u}) {
        for (uint32_t x : {2u, 30u, 64u, 127u}) {
            const float nDotV = (float(x) + 0.5f) / float(n), roughness = (float(y) + 0.5f) / float(n);
            const glm::vec2 ref = referenceDfg(nDotV, roughness, 8192);
            const glm::vec4 gpuValue = lut[size_t(y) * n + x];
            CAPTURE(x);
            CAPTURE(y);
            CHECK(gpuValue.x == doctest::Approx(ref.x).epsilon(0.03).scale(1.0));
            CHECK(gpuValue.y == doctest::Approx(ref.y).epsilon(0.03).scale(1.0));
            CHECK(gpuValue.y <= 1.01f); // Single scattering never exceeds 1 (energy is lost, not gained)
        }
    }
    // Smooth surface at normal incidence reflects everything specular: E(f0 = 1) ~ 1.
    CHECK(lut[size_t(0) * n + 127].y == doctest::Approx(1.0f).epsilon(0.02));
}

TEST_CASE("ibl: uniform environment bakes to the same radiance in every cube and mip") {
    zodar::test::ValidationGuard guard;
    Fixture f;
    const glm::vec3 radiance(0.5f, 0.25f, 1.0f);
    IblSettings settings = iblSettings(LightingQuality::Low);
    const auto env = f.scene.ibl().bake(uniformEnvironment(radiance, 128), settings);
    REQUIRE(env != nullptr);
    const auto check = [&](const GpuImage& image, uint32_t mip, uint32_t face, float tolerance) {
        const auto texels = toFloats(gpu().upload->readImage(image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, mip, face));
        for (const glm::vec4& t : texels) {
            REQUIRE(t.r == doctest::Approx(radiance.r).epsilon(tolerance));
            REQUIRE(t.g == doctest::Approx(radiance.g).epsilon(tolerance));
            REQUIRE(t.b == doctest::Approx(radiance.b).epsilon(tolerance));
        }
    };
    for (uint32_t face = 0; face < 6; ++face) {
        check(env->radiance(), 0, face, 0.002);
        check(env->radiance(), env->radianceMips() - 1, face, 0.002);
        check(env->irradiance(), 0, face, 0.002); // E / pi of a constant radiance is that radiance
        for (uint32_t mip = 0; mip < env->prefilteredMips(); ++mip) check(env->prefiltered(), mip, face, 0.002);
    }
    CHECK(env->gpuBytes() > 0);
}

TEST_CASE("ibl: white furnace - energy conservation with multiple-scattering compensation") {
    zodar::test::ValidationGuard guard;
    Fixture f;
    const auto env = f.scene.ibl().bake(uniformEnvironment(glm::vec3(1.0f), 128), iblSettings(LightingQuality::Low));
    const VkExtent2D extent{64, 64};
    SceneView view = sphereView();
    view.lighting.environment = env.get();
    view.lighting.drawEnvironmentBackground = false;
    for (float metallic : {0.0f, 1.0f}) {
        for (float roughness : {0.2f, 0.6f, 1.0f}) {
            MaterialDesc d;
            d.shader = f.scene.materials().pbrShader();
            d.baseColorFactor = glm::vec4(1.0f);
            d.metallic = metallic;
            d.roughness = roughness;
            const MaterialHandle m = f.scene.materials().createMaterial(d);
            const std::vector<MeshDraw> draws = {{&f.sphere, 0, m, glm::mat4(1)}};
            view.draws = draws;
            LightingSettings settings;
            settings.multiscatter = true;
            f.scene.setLightingSettings(settings);
            const glm::vec4 centre = f.render(view, extent)[32 * 64 + 32];
            settings.multiscatter = false;
            f.scene.setLightingSettings(settings);
            const glm::vec4 single = f.render(view, extent)[32 * 64 + 32];
            MESSAGE("metallic " << metallic << " roughness " << roughness << ": furnace " << centre.r
                                << " (single scattering " << single.r << ")");
            // A white, non-absorbing surface in a uniform white environment reflects ~1.
            CHECK(centre.r == doctest::Approx(1.0f).epsilon(0.04));
            CHECK(single.r <= centre.r + 1e-3f); // Single scattering loses energy, never gains it
        }
    }
}

TEST_CASE("ibl: procedural sky lights the scene, draws the background and is released safely") {
    zodar::test::ValidationGuard guard;
    Fixture f;
    ProceduralSkyDesc sky;
    sky.sunDirection = {0.0f, -1.0f, 0.0f}; // Sun straight up
    auto env = f.scene.ibl().bake(proceduralSky(256, sky), iblSettings(LightingQuality::Low));
    MaterialDesc d;
    d.shader = f.scene.materials().pbrShader();
    d.baseColorFactor = glm::vec4(0.8f);
    d.roughness = 0.9f;
    const MaterialHandle m = f.scene.materials().createMaterial(d);
    const std::vector<MeshDraw> draws = {{&f.sphere, 0, m, glm::mat4(1)}};
    SceneView view = sphereView();
    view.draws = draws;
    view.clearColor = {1.0f, 0.0f, 1.0f, 1.0f};
    view.lighting.environment = env.get();
    const VkExtent2D extent{64, 64};
    const auto image = f.render(view, extent);
    const glm::vec4 top = image[14 * 64 + 32], bottom = image[50 * 64 + 32], corner = image[1 * 64 + 1];
    CHECK(top.r > bottom.r);         // Lit from the sky above, darker from the ground below
    CHECK(corner.r != 1.0f);         // Background is the sky, not the clear colour
    CHECK(corner.b > corner.r);      // Blue-ish sky
    view.lighting.drawEnvironmentBackground = false;
    const auto noSky = f.render(view, extent);
    CHECK(noSky[1 * 64 + 1].r == 1.0f); // Clear colour
    CHECK(noSky[14 * 64 + 32] == top);  // Background toggle does not change lighting

    // Environment destruction is deferred: render without it right after.
    view.lighting.environment = nullptr;
    env.reset();
    f.render(view, extent);
    f.render(view, extent);
}
