#pragma once

#include "Runtime/Application.h"

#include <filesystem>
#include <memory>

namespace lastlight {

// The bunker level (Phase D2, GDD 10): game/assets/levels/bunker.scene.json loaded through the
// engine's AssetManager (async decode, registry ids) and LiveScene (hot reload of the level,
// its prefabs, materials, textures and models in Debug builds). Static greybox: fly camera
// only; no NPC, merchant, locker or power logic yet.
std::unique_ptr<zodar::Application> makeBunkerApp(const zodar::ApplicationDesc& desc,
                                                  std::filesystem::path assetDir);

} // namespace lastlight
