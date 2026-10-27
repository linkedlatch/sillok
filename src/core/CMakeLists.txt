# Model-independent logic. Depends on the C++ standard library only; no
# third-party include directory is visible here (docs/DESIGN.md D1).
add_library(sillok_core STATIC
    audio_buffer.cpp
    export.cpp
    files.cpp
    language.cpp
    pipeline.cpp
    postprocess.cpp
    prompt.cpp
    resampler.cpp
    segmenter.cpp
    sha256.cpp
    speakers.cpp
    stitcher.cpp
    utf8.cpp
    words.cpp)
target_include_directories(sillok_core PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_compile_features(sillok_core PUBLIC cxx_std_20)
sillok_set_warnings(sillok_core)
