#pragma once

#include "Runtime/Application.h"

#include <filesystem>
#include <memory>

namespace lastlight {

// The bunker level (Phase V5, GDD 10): game/assets/levels/bunker.scene.json loaded through the
// engine's AssetManager (async decode, registry ids) and LiveScene (hot reload of the level,
// its prefabs, materials, textures and models in Debug builds). Static greybox; no NPC,
// merchant, locker or power logic yet.
// Camera: fly camera (default), or with `explore` a TEMPORARY first-person walker with
// hybrid voxel/model collision (ExplorationWalker + CollisionWorld) until the D7 player controller.
std::unique_ptr<zodar::Application> makeBunkerApp(const zodar::ApplicationDesc& desc,
                                                  std::filesystem::path assetDir, bool explore, bool walkthrough = false);

} // namespace lastlight
