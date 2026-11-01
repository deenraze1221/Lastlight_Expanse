# Scene and material JSON files (Phase B2): *.scene.json / *.material.json parsing with
# validation (DataError: file, line, JSON path), deterministic serialization, material
# creation in MaterialSystem and instantiation into a World (MeshRenderer from WorldRender).
# nlohmann/json is private: public headers expose only engine types.
add_library(ZodarSceneFile STATIC
    SceneFile/DataError.cpp
    SceneFile/BlockFile.cpp
    SceneFile/MaterialFile.cpp
    SceneFile/SceneFile.cpp)
add_library(Zodar::SceneFile ALIAS ZodarSceneFile)

target_include_directories(ZodarSceneFile PUBLIC ${ZODAR_ENGINE_DIR}/rendering/SceneFile)
target_link_libraries(ZodarSceneFile PUBLIC Zodar::WorldRender Zodar::Scene PRIVATE nlohmann_json::nlohmann_json)
zodar_set_warnings(ZodarSceneFile)
