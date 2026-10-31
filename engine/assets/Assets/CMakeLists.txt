# CPU-side asset data and importers. No GPU dependencies: usable by tools and tests.
add_library(ZodarAssets STATIC
    GltfLoader.cpp
    ImageData.cpp
    Ktx2.cpp
    TextureCache.cpp
    TextureStandards.cpp
    EnvironmentMaps.cpp
    MeshData.cpp
    Primitives.cpp)
add_library(Zodar::Assets ALIAS ZodarAssets)
target_include_directories(ZodarAssets PUBLIC ${ZODAR_ENGINE_DIR}/assets)

target_link_libraries(ZodarAssets PUBLIC Zodar::Core PRIVATE Zodar::stb Zodar::cgltf Zodar::bcdec)
zodar_set_warnings(ZodarAssets)
