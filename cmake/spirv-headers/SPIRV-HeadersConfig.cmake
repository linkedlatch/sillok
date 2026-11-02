# Stands in for the package config that an installed SPIRV-Headers provides,
# for Android builds (cmake/third_party.cmake): ggml's Vulkan backend asks for
# the package, and the NDK has none.
if(NOT TARGET SPIRV-Headers::SPIRV-Headers)
    add_library(SPIRV-Headers::SPIRV-Headers INTERFACE IMPORTED)
    set_target_properties(SPIRV-Headers::SPIRV-Headers PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/../../third_party/SPIRV-Headers/include")
endif()
