# FFmpeg (third_party/ffmpeg, release n9.0.2) reads the apps' audio and video
# files: only its demuxers, audio decoders and parsers, without GPL or nonfree
# parts. It is built as shared libraries next to sillok, which keeps the LGPL
# simple: users can replace them. FFmpeg builds with its own configure script
# and make, so this needs a POSIX shell: on Windows, MSYS2's (with make and
# diffutils installed), run from an MSVC developer environment.
#
# Defines sillok_ffmpeg, an interface target with libavformat, libavcodec and
# libavutil, and SILLOK_FFMPEG_RUNTIME, the shared libraries to ship
# (those and libswresample).

set(ffmpeg_source "${SILLOK_THIRD_PARTY_DIR}/ffmpeg")
if(NOT EXISTS "${ffmpeg_source}/configure")
    message(FATAL_ERROR "${ffmpeg_source} is missing; run git submodule update --init")
endif()
set(ffmpeg_dir "${CMAKE_BINARY_DIR}/third_party/ffmpeg")
set(ffmpeg_install "${ffmpeg_dir}/install")

# Formats people record or download lectures and meetings in.
set(ffmpeg_demuxers aac aiff amr asf avi caf flac matroska mov mp3 mpegts ogg w64 wav)
set(ffmpeg_decoders
    aac aac_latm ac3 eac3 alac amrnb amrwb flac mp2 mp2float mp3 mp3float opus vorbis
    wmav1 wmav2 wmapro adpcm_ima_wav adpcm_ms pcm_alaw pcm_mulaw pcm_u8
    pcm_s16le pcm_s16be pcm_s24le pcm_s24be pcm_s32le pcm_s32be
    pcm_f32le pcm_f32be pcm_f64le pcm_f64be)
set(ffmpeg_parsers aac aac_latm ac3 amr flac mpegaudio opus vorbis)
list(JOIN ffmpeg_demuxers "," ffmpeg_demuxers)
list(JOIN ffmpeg_decoders "," ffmpeg_decoders)
list(JOIN ffmpeg_parsers "," ffmpeg_parsers)

# configure runs in a POSIX shell, which wants /c/... rather than C:/... paths.
function(ffmpeg_shell_path out path)
    if(CMAKE_HOST_WIN32)
        string(REGEX REPLACE "^([A-Za-z]):" "/\\1" path "${path}")
    endif()
    set(${out} "${path}" PARENT_SCOPE)
endfunction()
ffmpeg_shell_path(source_sh "${ffmpeg_source}")
ffmpeg_shell_path(install_sh "${ffmpeg_install}")

set(ffmpeg_args
    --prefix=${install_sh}
    --enable-shared --disable-static --enable-pic
    --disable-programs --disable-doc --disable-debug --disable-network
    --disable-autodetect --disable-everything
    # libswresample only because FFmpeg's Opus decoder needs it.
    --disable-avdevice --disable-avfilter --disable-swscale
    # x86 assembly needs nasm; audio decoding is fast without it.
    --disable-x86asm
    --enable-protocol=file
    --enable-demuxer=${ffmpeg_demuxers}
    --enable-decoder=${ffmpeg_decoders}
    --enable-parser=${ffmpeg_parsers})

if(APPLE)
    list(APPEND ffmpeg_args --install-name-dir=@rpath)
endif()
if(ANDROID)
    list(APPEND ffmpeg_args
        --enable-cross-compile --target-os=android --arch=${CMAKE_SYSTEM_PROCESSOR}
        --cc=${CMAKE_C_COMPILER} --ar=${CMAKE_AR} --nm=${CMAKE_NM}
        --ranlib=${CMAKE_RANLIB} --strip=${CMAKE_STRIP} --sysroot=${CMAKE_SYSROOT}
        --extra-cflags=--target=${CMAKE_C_COMPILER_TARGET}
        # Android 15 devices may use 16 KB pages.
        "--extra-ldflags=--target=${CMAKE_C_COMPILER_TARGET} -Wl,-z,max-page-size=16384")
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    execute_process(COMMAND xcrun --sdk ${CMAKE_OSX_SYSROOT} --show-sdk-path
        OUTPUT_VARIABLE sdk OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    set(target "arm64-apple-ios${CMAKE_OSX_DEPLOYMENT_TARGET}")
    if(sdk MATCHES "Simulator")
        string(APPEND target "-simulator")
    endif()
    list(APPEND ffmpeg_args
        --enable-cross-compile --target-os=darwin --arch=arm64 --cc=${CMAKE_C_COMPILER}
        "--extra-cflags=-target ${target} -isysroot ${sdk}"
        "--extra-ldflags=-target ${target} -isysroot ${sdk}")
elseif(APPLE)
    list(APPEND ffmpeg_args --cc=${CMAKE_C_COMPILER})
    if(CMAKE_OSX_DEPLOYMENT_TARGET)
        list(APPEND ffmpeg_args
            --extra-cflags=-mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}
            --extra-ldflags=-mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET})
    endif()
elseif(MSVC)
    # cl or clang-cl from the developer environment. Arm assembly would need
    # gas-preprocessor; without it FFmpeg uses its C code.
    get_filename_component(cc "${CMAKE_C_COMPILER}" NAME_WE)
    list(APPEND ffmpeg_args --toolchain=msvc --cc=${cc} --disable-asm)
    if(CMAKE_MSVC_RUNTIME_LIBRARY MATCHES "^MultiThreaded$")
        list(APPEND ffmpeg_args --extra-cflags=-MT)
    endif()
else()
    list(APPEND ffmpeg_args --cc=${CMAKE_C_COMPILER})
endif()

# The build runs from scripts that ExternalProject calls through the shell.
if(CMAKE_HOST_WIN32)
    find_program(SILLOK_FFMPEG_SHELL bash
        PATHS "C:/msys64/usr/bin" NO_DEFAULT_PATH REQUIRED
        DOC "MSYS2's bash, with make and diffutils, to build FFmpeg")
    set(shell_setup "export PATH=\"/usr/bin:$PATH\"\n")
else()
    find_program(SILLOK_FFMPEG_SHELL sh REQUIRED)
    set(shell_setup "")
endif()
include(ProcessorCount)
ProcessorCount(jobs)
if(jobs EQUAL 0)
    set(jobs 4)
endif()
list(JOIN ffmpeg_args "' '" quoted)
# Written only when they change, so that FFmpeg is configured again only then.
file(CONFIGURE OUTPUT "${ffmpeg_dir}/configure.sh"
    CONTENT "${shell_setup}exec '${source_sh}/configure' '${quoted}'\n" @ONLY)
file(CONFIGURE OUTPUT "${ffmpeg_dir}/build.sh"
    CONTENT "${shell_setup}make -j${jobs} && make install\n" @ONLY)

# Library files, named with the major versions of this FFmpeg release.
set(ffmpeg_libs avformat avcodec swresample avutil)
set(ffmpeg_version_files
    avformat version_major.h
    avcodec version_major.h
    swresample version_major.h
    avutil version.h)
set(SILLOK_FFMPEG_RUNTIME "")
set(byproducts "")
file(MAKE_DIRECTORY "${ffmpeg_install}/include")
add_library(sillok_ffmpeg INTERFACE)
foreach(lib IN LISTS ffmpeg_libs)
    list(FIND ffmpeg_version_files ${lib} index)
    math(EXPR index "${index} + 1")
    list(GET ffmpeg_version_files ${index} header)
    string(TOUPPER ${lib} upper)
    file(STRINGS "${ffmpeg_source}/lib${lib}/${header}" line
        REGEX "#define LIB${upper}_VERSION_MAJOR +[0-9]+")
    string(REGEX MATCH "[0-9]+$" major "${line}")
    if(WIN32)
        set(runtime "${ffmpeg_install}/bin/${lib}-${major}.dll")
    elseif(APPLE)
        set(runtime "${ffmpeg_install}/lib/lib${lib}.${major}.dylib")
    elseif(ANDROID)
        set(runtime "${ffmpeg_install}/lib/lib${lib}.so")  # unversioned on Android
    else()
        set(runtime "${ffmpeg_install}/lib/lib${lib}.so.${major}")
    endif()
    add_library(ffmpeg::${lib} SHARED IMPORTED GLOBAL)
    set_target_properties(ffmpeg::${lib} PROPERTIES
        IMPORTED_LOCATION "${runtime}"
        INTERFACE_INCLUDE_DIRECTORIES "${ffmpeg_install}/include")
    list(APPEND byproducts "${runtime}")
    if(WIN32)
        # FFmpeg installs the import library next to the DLL.
        set_target_properties(ffmpeg::${lib} PROPERTIES
            IMPORTED_IMPLIB "${ffmpeg_install}/bin/${lib}.lib")
        list(APPEND byproducts "${ffmpeg_install}/bin/${lib}.lib")
    elseif(APPLE)
        # Tells CMake to give the binaries linking it a search path for @rpath.
        set_target_properties(ffmpeg::${lib} PROPERTIES
            IMPORTED_SONAME "@rpath/lib${lib}.${major}.dylib")
    endif()
    list(APPEND SILLOK_FFMPEG_RUNTIME "${runtime}")
    target_link_libraries(sillok_ffmpeg INTERFACE ffmpeg::${lib})
endforeach()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # The build-tree search path must also reach the libraries FFmpeg's own
    # libraries load (libswresample for libavcodec): DT_RPATH applies to
    # those, DT_RUNPATH only to the binary's direct dependencies.
    target_link_options(sillok_ffmpeg INTERFACE "LINKER:--disable-new-dtags")
endif()

include(ExternalProject)
ExternalProject_Add(ffmpeg_build
    SOURCE_DIR "${ffmpeg_source}"
    BINARY_DIR "${ffmpeg_dir}/build"
    INSTALL_DIR "${ffmpeg_install}"
    DOWNLOAD_COMMAND ""
    UPDATE_COMMAND ""
    CONFIGURE_COMMAND "${SILLOK_FFMPEG_SHELL}" "${ffmpeg_dir}/configure.sh"
    BUILD_COMMAND "${SILLOK_FFMPEG_SHELL}" "${ffmpeg_dir}/build.sh"
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS ${byproducts}
    # FFmpeg's own warnings stay in the log files unless a step fails.
    LOG_CONFIGURE ON
    LOG_BUILD ON
    LOG_OUTPUT_ON_FAILURE ON)
ExternalProject_Add_StepDependencies(ffmpeg_build configure "${ffmpeg_dir}/configure.sh")
add_dependencies(sillok_ffmpeg ffmpeg_build)

# Windows finds DLLs next to the executable: copies FFmpeg's (and sillok's)
# there, for tests and the CLI to run from the build tree.
function(sillok_copy_runtime_dlls target)
    if(WIN32)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "$<TARGET_RUNTIME_DLLS:${target}>" "$<TARGET_FILE_DIR:${target}>"
            COMMAND_EXPAND_LISTS)
    endif()
endfunction()
