include(GoogleTest)

# A cross-compiled test binary cannot run on the build machine; list its tests
# when ctest runs them (through CMAKE_CROSSCOMPILING_EMULATOR, if set) instead.
if(CMAKE_CROSSCOMPILING)
    set(CMAKE_GTEST_DISCOVER_TESTS_DISCOVERY_MODE PRE_TEST)
endif()

# Model-free tests: the core logic driven by fake models (docs/DESIGN.md D1).
add_library(sillok_fakes STATIC fakes/fake_models.cpp)
target_include_directories(sillok_fakes PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(sillok_fakes PUBLIC sillok_core)
sillok_set_warnings(sillok_fakes)

add_executable(sillok_unit_tests
    unit/audio_test.cpp
    unit/language_prompt_test.cpp
    unit/output_test.cpp
    unit/pipeline_test.cpp
    unit/segmenter_test.cpp
    unit/text_test.cpp)
target_link_libraries(sillok_unit_tests PRIVATE sillok_fakes GTest::gtest_main)
sillok_set_warnings(sillok_unit_tests)
gtest_discover_tests(sillok_unit_tests)

# The C ABI end to end, over the fake models.
add_executable(sillok_capi_tests unit/capi_test.cpp fakes/fake_backend.cpp)
target_link_libraries(sillok_capi_tests PRIVATE
    sillok_capi sillok_fakes nlohmann_json::nlohmann_json GTest::gtest_main)
sillok_set_warnings(sillok_capi_tests)
gtest_discover_tests(sillok_capi_tests)

# src/core may include only the standard library and other core headers.
add_test(NAME core_includes_only_std
    COMMAND "${CMAKE_COMMAND}" -DCORE_DIR=${PROJECT_SOURCE_DIR}/src/core
            -P "${CMAKE_CURRENT_SOURCE_DIR}/check_core_includes.cmake")

# Integration tests with the real models; they skip without model files and
# test recordings.
if(TARGET sillok_models)
    set(SILLOK_TEST_MODELS_DIR "${PROJECT_SOURCE_DIR}/models" CACHE PATH "Models for integration tests")
    set(SILLOK_TEST_AUDIO_DIR "${PROJECT_SOURCE_DIR}/tests/data/audio" CACHE PATH "Recordings for integration tests")
    add_executable(sillok_integration_tests integration/models_test.cpp "${PROJECT_SOURCE_DIR}/apps/cli/wav_reader.cpp")
    target_include_directories(sillok_integration_tests PRIVATE "${PROJECT_SOURCE_DIR}/apps/cli")
    target_compile_definitions(sillok_integration_tests PRIVATE
        SILLOK_TEST_MODELS_DIR="${SILLOK_TEST_MODELS_DIR}"
        SILLOK_TEST_AUDIO_DIR="${SILLOK_TEST_AUDIO_DIR}")
    target_link_libraries(sillok_integration_tests PRIVATE sillok_models GTest::gtest_main)
    sillok_set_warnings(sillok_integration_tests)
    gtest_discover_tests(sillok_integration_tests DISCOVERY_TIMEOUT 60 PROPERTIES LABELS integration TIMEOUT 1800)
endif()

# Measurements with the real models (scripts/eval), on the shared library.
if(TARGET sillok)
    add_executable(sillok_probe integration/probe.cpp "${PROJECT_SOURCE_DIR}/apps/cli/wav_reader.cpp")
    target_include_directories(sillok_probe PRIVATE "${PROJECT_SOURCE_DIR}/apps/cli")
    target_link_libraries(sillok_probe PRIVATE sillok nlohmann_json::nlohmann_json)
    sillok_set_warnings(sillok_probe)
endif()
