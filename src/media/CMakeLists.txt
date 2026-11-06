# Reading audio and video files and converting pushed audio to 16 kHz mono
# with FFmpeg (cmake/ffmpeg.cmake), for the C ABI.
add_library(sillok_media STATIC audio_file.cpp audio_input.cpp loudness_filter.cpp)
target_link_libraries(sillok_media PUBLIC sillok_core PRIVATE sillok_ffmpeg)
sillok_set_warnings(sillok_media)
