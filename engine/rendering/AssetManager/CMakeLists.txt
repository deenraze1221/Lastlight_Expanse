# Runtime asset manager (Phase C): cached, reference-counted GPU textures (PNG/JPEG...), glTF
# models and material files with textures; unloading through the device deletion queue.
# AssetRegistry (C3): assets.json ids, import settings, dependencies, hashes, checks (CPU only).
# LiveScene (C6): scene file kept in sync with a World (hot reload).
# Depends on Renderer (GPU resources) and SceneFile (material/scene JSON).
add_library(ZodarAssetManager STATIC
    AssetManager/AssetManager.cpp
    AssetManager/AssetRegistry.cpp
    AssetManager/BlockMaterials.cpp
    AssetManager/LiveScene.cpp)
add_library(Zodar::AssetManager ALIAS ZodarAssetManager)

target_include_directories(ZodarAssetManager PUBLIC ${ZODAR_ENGINE_DIR}/rendering/AssetManager)
target_link_libraries(ZodarAssetManager PUBLIC Zodar::SceneFile Zodar::Renderer Zodar::Assets
    PRIVATE nlohmann_json::nlohmann_json)
zodar_set_warnings(ZodarAssetManager)
