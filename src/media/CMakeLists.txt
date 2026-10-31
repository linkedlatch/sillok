# Reading audio and video files with FFmpeg (cmake/ffmpeg.cmake), for the C
# ABI's sillok_job_push_file.
add_library(sillok_media STATIC audio_file.cpp)
target_link_libraries(sillok_media PUBLIC sillok_core PRIVATE sillok_ffmpeg)
sillok_set_warnings(sillok_media)
