# Phase C example: loads examples/asset_loading (glTF crate, PNG textures, material and scene
# files) through Zodar::AssetManager and renders it. ctest: gpu.asset_loading_example.
add_executable(ZodarAssetLoading main.cpp)
target_link_libraries(ZodarAssetLoading PRIVATE Zodar::Runtime Zodar::AssetManager)
target_compile_definitions(ZodarAssetLoading PRIVATE ZODAR_EXAMPLES_DIR="${ZODAR_ENGINE_DIR}/../examples")
zodar_set_warnings(ZodarAssetLoading)
