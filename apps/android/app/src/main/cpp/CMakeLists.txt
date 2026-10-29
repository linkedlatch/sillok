# The JNI bridge between the Android app and the sillok library. Gradle
# configures this project for each ABI; it builds the library from the
# repository root and packages libsillok.so next to libsillok_jni.so.
cmake_minimum_required(VERSION 3.28)
project(sillok_android LANGUAGES C CXX)

cmake_path(SET SILLOK_ROOT NORMALIZE "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../..")
set(SILLOK_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(SILLOK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
# Phones: ARMv8.2 with the dot product and fp16 extensions. Cross builds do
# not tune for the build machine, so x86_64 (the emulator) gets the baseline.
if(ANDROID_ABI STREQUAL "arm64-v8a")
    set(GGML_CPU_ARM_ARCH "armv8.2-a+dotprod+fp16" CACHE STRING "" FORCE)
endif()
add_subdirectory("${SILLOK_ROOT}" sillok EXCLUDE_FROM_ALL)

add_library(sillok_jni SHARED sillok_jni.cpp)
target_link_libraries(sillok_jni PRIVATE sillok)
sillok_set_warnings(sillok_jni)
