cmake_minimum_required(VERSION 3.21)
project(SillokNative LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(SILLOK_BUILD_TESTS "Build sillok native tests" ON)

set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
if(APPLE)
    set(GGML_METAL ON CACHE BOOL "" FORCE)
    set(GGML_METAL_EMBED_LIBRARY ON CACHE BOOL "" FORCE)
    set(WHISPER_COREML ON CACHE BOOL "" FORCE)
    set(WHISPER_COREML_ALLOW_FALLBACK ON CACHE BOOL "" FORCE)
endif()
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Vendor/whisper.cpp ${CMAKE_BINARY_DIR}/whisper.cpp EXCLUDE_FROM_ALL)

find_package(ZLIB REQUIRED)

set(NEMO ${CMAKE_CURRENT_SOURCE_DIR}/../Vendor/NeMo-Speech.cpp/src)
add_library(nemo_diar STATIC
    ${NEMO}/asr/diar/aosc_state.cpp
    ${NEMO}/asr/diar/diar_pipeline.cpp
    ${NEMO}/asr/diar/diarizer.cpp
    ${NEMO}/asr/diar/sortformer_model.cpp
    ${NEMO}/asr/encoder/fastconformer.cpp
    ${NEMO}/asr/encoder/rel_pos_attention.cpp
    ${NEMO}/asr/encoder/rope_transformer.cpp
    ${NEMO}/asr/encoder/transformer_encoder.cpp
    ${NEMO}/asr/features/fe.cpp
    ${NEMO}/common/audio_resampler.cpp
    ${NEMO}/common/parameter_parser.cpp
    ${NEMO}/runtime/ggml/backend.cpp
    ${NEMO}/runtime/ggml/loader.cpp
    ${NEMO}/runtime/ggml/logging.cpp
    ${NEMO}/runtime/ggml/nn.cpp
    ${NEMO}/runtime/ggml/session.cpp
    ${NEMO}/runtime/ggml/tensor_container.cpp
)
target_include_directories(nemo_diar PUBLIC
    ${NEMO}/asr ${NEMO}/asr/diar ${NEMO}/asr/encoder ${NEMO}/asr/features ${NEMO}/common ${NEMO}/runtime/ggml
)
target_link_libraries(nemo_diar PUBLIC ggml)

add_library(sillok_native STATIC
    src/types.cpp
    src/chunking.cpp
    src/segments.cpp
    src/language.cpp
    src/transcriber.cpp
    src/whisper_engine.cpp
    src/diarization.cpp
    src/c_api.cpp
)
target_include_directories(sillok_native
    PUBLIC include
    PRIVATE src third_party
)
target_link_libraries(sillok_native PUBLIC whisper ZLIB::ZLIB PRIVATE nemo_diar)

if(SILLOK_BUILD_TESTS)
    enable_testing()
    add_executable(sillok_native_tests
        tests/main.cpp
        tests/chunking_tests.cpp
        tests/segments_tests.cpp
        tests/language_tests.cpp
        tests/transcriber_tests.cpp
        tests/json_tests.cpp
        tests/engine_tests.cpp
        tests/diarization_tests.cpp
    )
    target_include_directories(sillok_native_tests PRIVATE src third_party include)
    target_link_libraries(sillok_native_tests PRIVATE sillok_native)
    target_compile_definitions(sillok_native_tests PRIVATE SILLOK_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")
    add_test(NAME sillok_native_tests COMMAND sillok_native_tests)

    add_executable(sillok_exit_during_inference tests/exit_during_inference.cpp)
    target_include_directories(sillok_exit_during_inference PRIVATE src third_party include tests)
    target_link_libraries(sillok_exit_during_inference PRIVATE sillok_native)
    target_compile_definitions(sillok_exit_during_inference PRIVATE SILLOK_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")
    add_test(NAME sillok_exit_during_inference COMMAND sillok_exit_during_inference)
    add_test(NAME sillok_exit_during_diarization COMMAND sillok_exit_during_inference diarize)
endif()
