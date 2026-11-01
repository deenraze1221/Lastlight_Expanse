# World content that is independent of the GPU: procedural noise, voxel chunk storage,
# greedy meshing with ambient occlusion, distance-based LOD and chunk streaming.
# Produces Assets::MeshData; the renderer uploads it (Renderer/StreamedMeshSet).
add_library(ZodarScene STATIC
    Scene/Noise.cpp
    Scene/Voxel/VoxelChunk.cpp
    Scene/Voxel/BlockRegistry.cpp
    Scene/Voxel/Shapes.cpp
    Scene/Voxel/ChunkMesher.cpp
    Scene/Voxel/VoxelWorld.cpp)
add_library(Zodar::Scene ALIAS ZodarScene)

target_include_directories(ZodarScene PUBLIC ${ZODAR_ENGINE_DIR}/scene)
target_link_libraries(ZodarScene PUBLIC Zodar::Core Zodar::Assets)
zodar_set_warnings(ZodarScene)
