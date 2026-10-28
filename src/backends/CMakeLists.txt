# Model backends: the only code that sees third-party headers (docs/DESIGN.md
# D1). One target per model; models.cpp decides which models are used.
add_library(sillok_ggml_devices STATIC ggml_devices.cpp)
target_link_libraries(sillok_ggml_devices PUBLIC sillok_core PRIVATE ggml)

add_library(sillok_whisper STATIC whisper/whisper_transcriber.cpp)
target_link_libraries(sillok_whisper PUBLIC sillok_ggml_devices PRIVATE whisper)

add_library(sillok_silero STATIC silero/silero_voice_detector.cpp)
target_link_libraries(sillok_silero PUBLIC sillok_core PRIVATE whisper)

add_library(sillok_sortformer STATIC sortformer/sortformer_diarizer.cpp)
target_link_libraries(sillok_sortformer PUBLIC sillok_ggml_devices PRIVATE nemo_diar)

add_library(sillok_models STATIC models.cpp)
target_link_libraries(sillok_models
    PUBLIC sillok_whisper sillok_silero sillok_sortformer
    PRIVATE whisper nemo_diar)

if(SILLOK_COREML)
    target_compile_definitions(sillok_whisper PRIVATE SILLOK_COREML)
    target_compile_definitions(sillok_models PRIVATE SILLOK_COREML)
endif()
foreach(target sillok_ggml_devices sillok_whisper sillok_silero sillok_sortformer sillok_models)
    sillok_set_warnings(${target})
endforeach()
