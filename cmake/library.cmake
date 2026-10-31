# The products: the C ABI over the real models, as one shared library and one
# static archive. Both export only the sillok_* functions. They need FFmpeg's
# shared libraries (SILLOK_FFMPEG_RUNTIME) beside them.

add_library(sillok SHARED)
# Apps link FFmpeg's libraries too, so that the linker and the build tree's
# search path reach the ones sillok's FFmpeg libraries load.
target_link_libraries(sillok PRIVATE sillok_capi sillok_models INTERFACE sillok_ffmpeg)
target_include_directories(sillok PUBLIC "${PROJECT_SOURCE_DIR}/include")
set_target_properties(sillok PROPERTIES VERSION ${PROJECT_VERSION} SOVERSION 0)
if(APPLE)
    target_link_options(sillok PRIVATE
        "LINKER:-exported_symbols_list,${CMAKE_CURRENT_LIST_DIR}/sillok.exports")
elseif(UNIX)
    # Third-party archives were built with default visibility; keep them local.
    target_link_options(sillok PRIVATE
        "LINKER:--version-script=${CMAKE_CURRENT_LIST_DIR}/sillok.map"
        "LINKER:--exclude-libs,ALL")
endif()

# Static archive with sillok and its third-party code, for apps that link
# statically (iOS in particular). The app still links the system libraries
# (OpenMP, Vulkan, Apple frameworks) these depend on, and FFmpeg's shared
# libraries (SILLOK_FFMPEG_RUNTIME).
add_library(sillok_capi_archive STATIC)
target_link_libraries(sillok_capi_archive PRIVATE sillok_capi)
set(bundled sillok_capi_archive sillok_models sillok_whisper sillok_silero
    sillok_sortformer sillok_ggml_devices sillok_media sillok_core whisper nemo_diar
    ggml ggml-base ggml-cpu)
foreach(optional ggml-vulkan ggml-metal ggml-blas)
    if(TARGET ${optional})
        list(APPEND bundled ${optional})
    endif()
endforeach()
set(archives "")
foreach(target IN LISTS bundled)
    list(APPEND archives "$<TARGET_FILE:${target}>")
endforeach()

set(bundle "${CMAKE_BINARY_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}sillok_static${CMAKE_STATIC_LIBRARY_SUFFIX}")
if(APPLE)
    set(merge libtool -static -o "${bundle}" ${archives})
elseif(MSVC)
    set(merge "${CMAKE_AR}" /NOLOGO "/OUT:${bundle}" ${archives})
else()
    list(TRANSFORM archives PREPEND "addlib " OUTPUT_VARIABLE addlibs)
    list(JOIN addlibs "\n" addlibs)
    file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/sillok_static.mri"
         CONTENT "create ${bundle}\n${addlibs}\nsave\nend\n")
    set(merge sh -c "\"${CMAKE_AR}\" -M < \"${CMAKE_BINARY_DIR}/sillok_static.mri\"")
endif()
add_custom_command(OUTPUT "${bundle}"
    COMMAND ${CMAKE_COMMAND} -E rm -f "${bundle}"
    COMMAND ${merge}
    DEPENDS ${bundled}
    COMMENT "Bundling ${bundle}"
    VERBATIM)
add_custom_target(sillok_static ALL DEPENDS "${bundle}")
