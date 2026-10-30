# CPU-side asset data and importers. No GPU dependencies: usable by tools and tests.
add_library(ZodarAssets STATIC
    GltfLoader.cpp
    ImageData.cpp
    MeshData.cpp
    Primitives.cpp)
add_library(Zodar::Assets ALIAS ZodarAssets)

target_link_libraries(ZodarAssets PUBLIC Zodar::Core PRIVATE Zodar::stb Zodar::cgltf)
zodar_set_warnings(ZodarAssets)
