# The sillok command-line app: one of the apps on top of the C ABI, so it
# links the shared library and sees nothing but include/sillok/sillok.h.
add_executable(sillok_cli main.cpp paths.cpp process.cpp progress.cpp wav_reader.cpp)
set_target_properties(sillok_cli PROPERTIES OUTPUT_NAME sillok)
target_link_libraries(sillok_cli PRIVATE sillok nlohmann_json::nlohmann_json)
if(WIN32)
    target_compile_definitions(sillok_cli PRIVATE SILLOK_USE_SHARED)
endif()
sillok_set_warnings(sillok_cli)
