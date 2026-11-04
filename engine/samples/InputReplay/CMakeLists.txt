add_executable(ZodarInputReplay ${ZODAR_ENGINE_DIR}/../examples/input_replay/main.cpp)
target_link_libraries(ZodarInputReplay PRIVATE Zodar::Input Zodar::Gameplay)
target_compile_definitions(ZodarInputReplay PRIVATE
    ZODAR_INPUT_REPLAY_MAP="${ZODAR_ENGINE_DIR}/../examples/input_replay/input.json")
zodar_set_warnings(ZodarInputReplay)
if(ZODAR_BUILD_TESTS)
    add_test(NAME examples.input_replay
        COMMAND ZodarInputReplay ${CMAKE_BINARY_DIR}/example.inputrec.jsonl)
endif()
