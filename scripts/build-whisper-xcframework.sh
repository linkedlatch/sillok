#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/Vendor/whisper.cpp"
OUT="$ROOT/Vendor/whisper.xcframework"
BUILD="$ROOT/build/whisper"
CMAKE="$("$ROOT/scripts/bootstrap-tools.sh" | tail -n1)"

MACOS_MIN=14.0
IOS_MIN=17.0
JOBS=$(sysctl -n hw.ncpu)

COMMON_ARGS=(
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_OSX_ARCHITECTURES=arm64
    -DBUILD_SHARED_LIBS=OFF
    -DWHISPER_BUILD_EXAMPLES=OFF
    -DWHISPER_BUILD_TESTS=OFF
    -DWHISPER_BUILD_SERVER=OFF
    -DGGML_METAL=ON
    -DGGML_METAL_EMBED_LIBRARY=ON
    -DGGML_METAL_USE_BF16=ON
    -DGGML_BLAS_DEFAULT=ON
    -DGGML_NATIVE=OFF
    -DGGML_OPENMP=OFF
    -DWHISPER_COREML=ON
    -DWHISPER_COREML_ALLOW_FALLBACK=ON
)

build_slice() {
    local name=$1; shift
    local dir="$BUILD/$name"
    echo "==> $name"
    "$CMAKE" -S "$SRC" -B "$dir" -G "Unix Makefiles" "${COMMON_ARGS[@]}" "$@" >/dev/null
    "$CMAKE" --build "$dir" -j "$JOBS" >/dev/null
    libtool -static -no_warning_for_no_symbols -o "$dir/libwhisper-all.a" $(find "$dir" -name '*.a' ! -name 'libwhisper-all.a')
}

build_slice macos \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=$MACOS_MIN
build_slice ios \
    -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphoneos \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=$IOS_MIN
build_slice ios-simulator \
    -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphonesimulator \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=$IOS_MIN

HEADERS="$BUILD/headers"
rm -rf "$HEADERS" && mkdir -p "$HEADERS"
cp "$SRC/include/whisper.h" \
   "$SRC/ggml/include/ggml.h" \
   "$SRC/ggml/include/ggml-alloc.h" \
   "$SRC/ggml/include/ggml-backend.h" \
   "$SRC/ggml/include/ggml-cpu.h" \
   "$SRC/ggml/include/ggml-metal.h" \
   "$SRC/ggml/include/ggml-opt.h" \
   "$SRC/ggml/include/ggml-cpp.h" \
   "$SRC/ggml/include/gguf.h" \
   "$HEADERS/"
cat > "$HEADERS/module.modulemap" <<'MAP'
module whisper {
    header "whisper.h"
    header "ggml.h"
    header "ggml-alloc.h"
    header "ggml-backend.h"
    header "ggml-cpu.h"
    header "ggml-metal.h"
    header "ggml-opt.h"
    link "c++"
    link framework "Accelerate"
    link framework "CoreML"
    link framework "Foundation"
    link framework "Metal"
    export *
}
MAP

rm -rf "$OUT"
xcodebuild -create-xcframework \
    -library "$BUILD/macos/libwhisper-all.a" -headers "$HEADERS" \
    -library "$BUILD/ios/libwhisper-all.a" -headers "$HEADERS" \
    -library "$BUILD/ios-simulator/libwhisper-all.a" -headers "$HEADERS" \
    -output "$OUT"
echo "==> $OUT"
