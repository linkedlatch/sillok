# The product: the C ABI over the real models, as one shared library that
# exports only the sillok_* functions. It needs FFmpeg's shared libraries
# (SILLOK_FFMPEG_RUNTIME) beside it.

add_library(sillok SHARED)
# Apps link FFmpeg's libraries too, so that the linker and the build tree's
# search path reach the ones sillok's FFmpeg libraries load.
target_link_libraries(sillok PRIVATE sillok_capi sillok_models INTERFACE sillok_ffmpeg)
target_include_directories(sillok PUBLIC "${PROJECT_SOURCE_DIR}/include")
set_target_properties(sillok PROPERTIES VERSION ${PROJECT_VERSION} SOVERSION 0)
if(APPLE)
    # Apps that bundle the library (Flutter) rewrite its install name.
    target_link_options(sillok PRIVATE
        "LINKER:-exported_symbols_list,${CMAKE_CURRENT_LIST_DIR}/sillok.exports"
        "LINKER:-headerpad_max_install_names")
elseif(UNIX)
    # Third-party archives were built with default visibility; keep them local.
    target_link_options(sillok PRIVATE
        "LINKER:--version-script=${CMAKE_CURRENT_LIST_DIR}/sillok.map"
        "LINKER:--exclude-libs,ALL")
endif()
