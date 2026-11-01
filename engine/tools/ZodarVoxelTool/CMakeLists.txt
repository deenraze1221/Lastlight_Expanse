add_executable(ZodarVoxelTool main.cpp)
target_link_libraries(ZodarVoxelTool PRIVATE Zodar::SceneFile)
zodar_set_warnings(ZodarVoxelTool)
