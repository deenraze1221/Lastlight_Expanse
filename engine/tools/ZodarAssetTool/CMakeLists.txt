# Asset registry CLI (Phase C3): import / list / check against assets.json. CPU only.
add_executable(ZodarAssetTool main.cpp)
target_link_libraries(ZodarAssetTool PRIVATE Zodar::AssetManager Zodar::AssetPipeline)
zodar_set_warnings(ZodarAssetTool)
