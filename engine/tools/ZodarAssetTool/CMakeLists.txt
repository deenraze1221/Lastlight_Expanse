# Asset registry CLI (Phase C3): import / list / check against assets.json. CPU only.
add_library(ZodarAssetCommands STATIC main.cpp)
target_link_libraries(ZodarAssetCommands PUBLIC Zodar::AssetManager Zodar::AssetPipeline)
zodar_set_warnings(ZodarAssetCommands)
add_executable(ZodarAssetTool Entry.cpp)
target_link_libraries(ZodarAssetTool PRIVATE ZodarAssetCommands)
zodar_set_warnings(ZodarAssetTool)
