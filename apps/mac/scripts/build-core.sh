#!/bin/sh
set -eu

root=$(cd "$(dirname "$0")/../../.." && pwd)
build="$root/build/mac"
out="$root/apps/mac/Packages/SillokKit/Frameworks"
deployment_target=15.0
ffmpeg_libs="avformat avcodec avfilter swresample avutil"

cmake -S "$root" -B "$build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$deployment_target" \
    -DSILLOK_COREML=ON \
    -DSILLOK_BUILD_CLI=OFF \
    -DSILLOK_BUILD_TESTS=OFF
cmake --build "$build" --target sillok --parallel

version=$(sed -n 's/^project(sillok VERSION \([0-9.]*\).*/\1/p' "$root/CMakeLists.txt")
staging="$build/frameworks"
rm -rf "$staging"
mkdir -p "$staging" "$out"

framework_path() {
    echo "@rpath/$1.framework/Versions/A/$1"
}

make_framework() {
    name=$1
    dylib=$2
    dir="$staging/$name.framework"
    mkdir -p "$dir/Versions/A/Resources"
    cp "$dylib" "$dir/Versions/A/$name"
    chmod u+w "$dir/Versions/A/$name"
    cat > "$dir/Versions/A/Resources/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>en</string>
    <key>CFBundleExecutable</key>
    <string>$name</string>
    <key>CFBundleIdentifier</key>
    <string>io.github.linkedlatch.sillok.$name</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleName</key>
    <string>$name</string>
    <key>CFBundlePackageType</key>
    <string>FMWK</string>
    <key>CFBundleShortVersionString</key>
    <string>$version</string>
    <key>CFBundleVersion</key>
    <string>$version</string>
    <key>LSMinimumSystemVersion</key>
    <string>$deployment_target</string>
</dict>
</plist>
EOF
    ln -s A "$dir/Versions/Current"
    ln -s "Versions/Current/$name" "$dir/$name"
    ln -s Versions/Current/Resources "$dir/Resources"

    binary="$dir/Versions/A/$name"
    install_name_tool -id "$(framework_path "$name")" "$binary"
    for dependency in $ffmpeg_libs; do
        old=$(otool -L "$binary" | awk -v lib="$dependency" '$1 ~ "^@rpath/lib" lib "\\.[0-9]+\\.dylib$" { print $1 }')
        if [ -n "$old" ]; then
            install_name_tool -change "$old" "$(framework_path "$dependency")" "$binary"
        fi
    done
    codesign --force --sign - "$binary"
}

add_module() {
    name=$1
    header=$2
    dir="$staging/$name.framework"
    mkdir -p "$dir/Versions/A/Headers" "$dir/Versions/A/Modules"
    cp "$header" "$dir/Versions/A/Headers/"
    cat > "$dir/Versions/A/Modules/module.modulemap" <<EOF
framework module $name {
    header "$(basename "$header")"
    export *
}
EOF
    ln -s Versions/Current/Headers "$dir/Headers"
    ln -s Versions/Current/Modules "$dir/Modules"
}

make_xcframework() {
    name=$1
    rm -rf "$out/$name.xcframework"
    xcodebuild -create-xcframework \
        -framework "$staging/$name.framework" \
        -output "$out/$name.xcframework" >/dev/null
}

make_framework CSillok "$build/libsillok.dylib"
add_module CSillok "$root/include/sillok/sillok.h"
make_xcframework CSillok

for lib in $ffmpeg_libs; do
    dylib=$(ls "$build"/third_party/ffmpeg/install/lib/lib"$lib".[0-9]*.dylib | head -n 1)
    make_framework "$lib" "$dylib"
    make_xcframework "$lib"
done

echo "XCFrameworks in $out"
