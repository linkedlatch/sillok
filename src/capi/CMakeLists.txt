# The C ABI over the core. An object library, so that the shared and static
# products keep every exported function (cmake/library.cmake); the tests link
# the same objects with fake models.
add_library(sillok_capi OBJECT json_io.cpp sillok.cpp)
target_include_directories(sillok_capi PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(sillok_capi
    PUBLIC sillok_core sillok_media
    PRIVATE nlohmann_json::nlohmann_json)
target_compile_definitions(sillok_capi PRIVATE
    SILLOK_VERSION="${PROJECT_VERSION}"
    SILLOK_BUILD_SHARED)
set_target_properties(sillok_capi PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
sillok_set_warnings(sillok_capi)
