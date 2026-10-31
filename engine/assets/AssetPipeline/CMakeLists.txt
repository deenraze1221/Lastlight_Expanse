# Offline asset processing (texture compression, mip baking, model texture caches).
# Used by ZodarAssetCompiler and tests; the runtime renderer does not link it.
add_library(ZodarAssetPipeline STATIC
    TextureCompiler.cpp
    ModelCompiler.cpp)
add_library(Zodar::AssetPipeline ALIAS ZodarAssetPipeline)

target_link_libraries(ZodarAssetPipeline PUBLIC Zodar::Assets PRIVATE Zodar::bcenc)
zodar_set_warnings(ZodarAssetPipeline)
