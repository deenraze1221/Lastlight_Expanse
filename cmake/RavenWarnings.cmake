# Applies the engine warning policy to a first-party target. Third-party code is
# pulled in as SYSTEM and is not affected.
function(raven_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
        if(RAVEN_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor
            # Vulkan structs are initialized as `T x{sType}`; the remaining members are
            # value-initialized by the language, so this warning is pure noise here.
            -Wno-missing-field-initializers)
        if(RAVEN_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()
