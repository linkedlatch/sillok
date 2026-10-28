# Third-party code in third_party/: git submodules (`git submodule update
# --init`, not recursive) and the nlohmann/json header. Only src/backends,
# src/capi, apps and tests may link the targets defined here (docs/DESIGN.md
# D1); src/core never sees them.

set(SILLOK_THIRD_PARTY_DIR "${PROJECT_SOURCE_DIR}/third_party")

function(sillok_require_third_party name)
    if(NOT EXISTS "${SILLOK_THIRD_PARTY_DIR}/${name}/CMakeLists.txt")
        message(FATAL_ERROR "${SILLOK_THIRD_PARTY_DIR}/${name} is missing; run git submodule update --init")
    endif()
endfunction()

# Applies patches/<name>/*.patch to a submodule once (.gitmodules ignores the
# resulting changes).
function(sillok_apply_patches name)
    file(GLOB patches "${PROJECT_SOURCE_DIR}/patches/${name}/*.patch")
    if(NOT patches)
        return()
    endif()
    find_package(Git REQUIRED)
    foreach(patch IN LISTS patches)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" apply --reverse --check "${patch}"
            WORKING_DIRECTORY "${SILLOK_THIRD_PARTY_DIR}/${name}"
            RESULT_VARIABLE applied OUTPUT_QUIET ERROR_QUIET)
        if(NOT applied EQUAL 0)
            execute_process(
                COMMAND "${GIT_EXECUTABLE}" apply "${patch}"
                WORKING_DIRECTORY "${SILLOK_THIRD_PARTY_DIR}/${name}"
                RESULT_VARIABLE failed)
            if(failed)
                message(FATAL_ERROR "cannot apply ${patch}")
            endif()
            message(STATUS "Applied ${patch}")
        endif()
    endforeach()
endfunction()

# nlohmann/json v3.12.0 (55f93686), single_include/nlohmann/json.hpp only:
# used by the C ABI layer and the CLI.
add_library(nlohmann_json INTERFACE)
add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
target_include_directories(nlohmann_json SYSTEM INTERFACE "${SILLOK_THIRD_PARTY_DIR}/json/include")

if(SILLOK_BUILD_TESTS)
    sillok_require_third_party(googletest)
    set(BUILD_GMOCK OFF CACHE INTERNAL "")
    set(INSTALL_GTEST OFF CACHE INTERNAL "")
    add_subdirectory("${SILLOK_THIRD_PARTY_DIR}/googletest" "${CMAKE_BINARY_DIR}/third_party/googletest" EXCLUDE_FROM_ALL SYSTEM)
endif()

if(NOT SILLOK_WITH_MODELS)
    return()
endif()

# whisper.cpp builds the only ggml in the process; the NeMo diarization sources
# below link the same ggml target (docs/DESIGN.md D2). Everything is static so the
# sillok library is self-contained.
sillok_require_third_party(whisper.cpp)
sillok_apply_patches(whisper.cpp)
# whisper.cpp declares cmake_minimum_required(3.5), so without CMP0077 its
# option(BUILD_SHARED_LIBS) would override the value set here.
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
set(BUILD_SHARED_LIBS OFF)
set(WHISPER_BUILD_TESTS OFF CACHE INTERNAL "")
set(WHISPER_BUILD_EXAMPLES OFF CACHE INTERNAL "")
set(WHISPER_BUILD_SERVER OFF CACHE INTERNAL "")
set(WHISPER_COREML ${SILLOK_COREML} CACHE INTERNAL "")
# CPU runs load the model without a path so that no Core ML encoder is found;
# whisper.cpp then has to fall back to its own encoder (see backends/whisper).
set(WHISPER_COREML_ALLOW_FALLBACK ON CACHE INTERNAL "")
set(GGML_VULKAN ${SILLOK_VULKAN} CACHE INTERNAL "")
# Android and Windows have no OpenMP runtime the app could rely on (libomp.so,
# libomp.dll with clang-cl, vcomp140.dll with MSVC); ggml's own thread pool
# saves the app from shipping one.
if(ANDROID OR WIN32)
    set(GGML_OPENMP OFF CACHE INTERNAL "")
endif()
add_subdirectory("${SILLOK_THIRD_PARTY_DIR}/whisper.cpp" "${CMAKE_BINARY_DIR}/third_party/whisper.cpp" EXCLUDE_FROM_ALL SYSTEM)

# NeMo-Speech.cpp: only the sources behind nemo_speech::asr::Diarizer, built
# against the ggml above (docs/DESIGN.md D16). Its own CMake would add a second
# ggml and pull in the NeMo ASR model and SentencePiece.
sillok_require_third_party(NeMo-Speech.cpp)
set(nemo "${SILLOK_THIRD_PARTY_DIR}/NeMo-Speech.cpp/src")
add_library(nemo_diar STATIC
    "${nemo}/runtime/ggml/backend.cpp"
    "${nemo}/runtime/ggml/loader.cpp"
    "${nemo}/runtime/ggml/logging.cpp"
    "${nemo}/runtime/ggml/nn.cpp"
    "${nemo}/runtime/ggml/session.cpp"
    "${nemo}/runtime/ggml/tensor_container.cpp"
    "${nemo}/asr/diar/aosc_state.cpp"
    "${nemo}/asr/diar/diar_pipeline.cpp"
    "${nemo}/asr/diar/diarizer.cpp"
    "${nemo}/asr/diar/sortformer_model.cpp"
    "${nemo}/asr/encoder/fastconformer.cpp"
    "${nemo}/asr/encoder/rel_pos_attention.cpp"
    "${nemo}/asr/encoder/rope_transformer.cpp"
    "${nemo}/asr/encoder/transformer_encoder.cpp"
    "${nemo}/asr/features/fe.cpp"
    "${nemo}/common/audio_resampler.cpp"
    "${nemo}/common/parameter_parser.cpp")
target_include_directories(nemo_diar SYSTEM PUBLIC
    "${nemo}/asr"
    "${nemo}/asr/diar"
    "${nemo}/asr/encoder"
    "${nemo}/asr/features"
    "${nemo}/common"
    "${nemo}/runtime/ggml")
target_link_libraries(nemo_diar PUBLIC ggml)
set_target_properties(nemo_diar PROPERTIES
    CXX_STANDARD 17
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON)
# Upstream code we compile but do not maintain (unchecked fread in a debug
# dump, a printf format in a log line); keep the build output to our own
# warnings.
target_compile_options(nemo_diar PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/w,-w>)
