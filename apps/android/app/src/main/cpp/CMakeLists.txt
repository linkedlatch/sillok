# The JNI bridge between the Android app and the sillok library. Gradle
# configures this project for each ABI; it builds the library from the
# repository root and packages libsillok.so next to libsillok_jni.so.
cmake_minimum_required(VERSION 3.28)
project(sillok_android LANGUAGES C CXX)

cmake_path(SET SILLOK_ROOT NORMALIZE "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../..")
set(SILLOK_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(SILLOK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
# Phones: ARMv8.2 with the dot product and fp16 extensions. x86_64 is only
# for the emulator, which passes the host CPU's AVX2 through; the ABI's own
# baseline (SSE4.2) runs the models too slowly to test them.
if(ANDROID_ABI STREQUAL "arm64-v8a")
    set(GGML_CPU_ARM_ARCH "armv8.2-a+dotprod+fp16" CACHE STRING "" FORCE)
elseif(ANDROID_ABI STREQUAL "x86_64")
    foreach(feature GGML_SSE42 GGML_AVX GGML_AVX2 GGML_FMA GGML_F16C GGML_BMI2)
        set(${feature} ON CACHE BOOL "" FORCE)
    endforeach()
endif()
add_subdirectory("${SILLOK_ROOT}" sillok EXCLUDE_FROM_ALL)

add_library(sillok_jni SHARED sillok_jni.cpp)
target_link_libraries(sillok_jni PRIVATE sillok)
sillok_set_warnings(sillok_jni)
