# Entity world (Phase B): zodar::World / zodar::Entity, a thin API over EnTT so the ECS
# backend stays replaceable. CPU only: components, Name/Transform, hierarchy (B1.2); renderer bridge lives in Zodar::WorldRender.
add_library(ZodarWorld STATIC
    World/World.cpp
    World/Reflection.cpp)
add_library(Zodar::World ALIAS ZodarWorld)

target_include_directories(ZodarWorld PUBLIC ${ZODAR_ENGINE_DIR}/world)
target_link_libraries(ZodarWorld PUBLIC Zodar::Core Zodar::EnTT)
zodar_set_warnings(ZodarWorld)
