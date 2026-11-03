#pragma once

#include "Runtime/Application.h"

#include <filesystem>
#include <memory>

namespace lastlight {

// Playable voxel/model bunker. Fixed action-driven controller, interactions, sliding doors,
// room power and transactional save handlers live in game/. See BUNKER_GAMEPLAY.md.
// `explore` enables walking; false preserves the fly camera. `legacySpawn` selects the
// original entrance camera spawn used by exploration and walkthrough regressions.
std::unique_ptr<zodar::Application> makeBunkerApp(const zodar::ApplicationDesc& desc,
                                                  std::filesystem::path assetDir, bool explore, bool walkthrough = false, bool legacySpawn = false);

} // namespace lastlight
