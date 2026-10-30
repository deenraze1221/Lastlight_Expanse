# CPU-side asset data and importers. No GPU dependencies: usable by tools and tests.
add_library(RavenAssets STATIC
    GltfLoader.cpp
    ImageData.cpp
    MeshData.cpp
    Primitives.cpp)
add_library(Raven::Assets ALIAS RavenAssets)

target_link_libraries(RavenAssets PUBLIC Raven::Core PRIVATE Raven::stb Raven::cgltf)
raven_set_warnings(RavenAssets)
