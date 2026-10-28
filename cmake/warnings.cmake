# Strict warnings for sillok's own targets. Third-party targets keep their own
# flags and are included as SYSTEM, so their headers do not trigger these.

option(SILLOK_WERROR "Treat warnings in sillok's own code as errors" OFF)

function(sillok_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
        # The CRT deprecates standard functions such as getenv in favour of
        # its own _s variants; sillok uses the standard ones.
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
        if(SILLOK_WERROR)
            target_compile_options(${target} PRIVATE /WX)
        endif()
        return()
    endif()
    target_compile_options(${target} PRIVATE
        -Wall -Wextra -Wpedantic
        -Wconversion -Wsign-conversion -Wshadow
        -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual
        -Wcast-align -Wnull-dereference -Wdouble-promotion
        -Wformat=2 -Wimplicit-fallthrough)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${target} PRIVATE
            -Wduplicated-cond -Wduplicated-branches -Wlogical-op)
    endif()
    if(SILLOK_WERROR)
        target_compile_options(${target} PRIVATE -Werror)
    endif()
endfunction()
