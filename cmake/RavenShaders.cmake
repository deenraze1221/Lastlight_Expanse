# GLSL -> SPIR-V compilation at build time. glslc (Vulkan SDK / shaderc) is preferred;
# glslangValidator is accepted as a fallback.
find_program(RAVEN_GLSLC glslc HINTS $ENV{VULKAN_SDK}/bin $ENV{VULKAN_SDK}/Bin)
find_program(RAVEN_GLSLANG glslangValidator HINTS $ENV{VULKAN_SDK}/bin $ENV{VULKAN_SDK}/Bin)

if(NOT RAVEN_GLSLC AND NOT RAVEN_GLSLANG)
    message(FATAL_ERROR "No GLSL compiler found. Install the Vulkan SDK (glslc) or glslang-tools.")
endif()

# raven_compile_shaders(<target> SOURCES <files...>)
# Compiles each source to ${RAVEN_OUTPUT_DIR}/shaders/<name>.spv and makes <target> depend on it.
# Engine shader library: #include "raven/<file>.glsl" from any engine or game shader.
set(RAVEN_SHADER_INCLUDE_DIR ${PROJECT_SOURCE_DIR}/Shaders/Include)

function(raven_compile_shaders target)
    cmake_parse_arguments(ARG "" "" "SOURCES;INCLUDE_DIRS" ${ARGN})
    set(output_dir ${RAVEN_OUTPUT_DIR}/shaders)
    set(include_dirs ${RAVEN_SHADER_INCLUDE_DIR} ${ARG_INCLUDE_DIRS})
    set(include_args)
    set(include_files)
    foreach(dir IN LISTS include_dirs)
        list(APPEND include_args -I${dir})
        file(GLOB_RECURSE dir_files CONFIGURE_DEPENDS ${dir}/*.glsl)
        list(APPEND include_files ${dir_files})
    endforeach()
    set(outputs)
    foreach(source IN LISTS ARG_SOURCES)
        get_filename_component(source_abs ${source} ABSOLUTE)
        get_filename_component(name ${source} NAME)
        set(output ${output_dir}/${name}.spv)
        if(RAVEN_GLSLC)
            set(command ${RAVEN_GLSLC} --target-env=vulkan1.3 -Werror -O ${include_args} -o ${output} ${source_abs})
        else()
            set(command ${RAVEN_GLSLANG} -V --target-env vulkan1.3 ${include_args} -o ${output} ${source_abs})
        endif()
        add_custom_command(
            OUTPUT ${output}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${output_dir}
            COMMAND ${command}
            DEPENDS ${source_abs} ${include_files}
            COMMENT "Compiling shader ${name}"
            VERBATIM)
        list(APPEND outputs ${output})
    endforeach()
    add_custom_target(${target}_shaders DEPENDS ${outputs})
    add_dependencies(${target} ${target}_shaders)
endfunction()
