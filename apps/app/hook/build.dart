// Builds the sillok library (the repository's CMake project, with FFmpeg)
// for the platform the app is built for, and bundles it with FFmpeg's
// libraries as code assets. Flutter places them in each platform's app and
// fixes the references between them where needed (Apple frameworks).
//
// Windows builds need MSYS2's make for FFmpeg (README, Build), and use
// Vulkan when its SDK is found: hooks see only a few environment variables,
// so the hook looks in C:\VulkanSDK unless the pubspec names the SDK
// (hooks: user_defines: sillok: vulkan_sdk: <path>).
import 'dart:io';

import 'package:code_assets/code_assets.dart';
import 'package:hooks/hooks.dart';
import 'package:logging/logging.dart';
import 'package:native_toolchain_cmake/native_toolchain_cmake.dart';

void main(List<String> args) async {
  await build(args, (input, output) async {
    if (!input.config.buildCodeAssets) return;
    final logger = Logger('sillok')..onRecord.listen((record) => stderr.writeln(record.message));
    final code = input.config.code;
    final os = code.targetOS;
    final root = input.packageRoot.resolve('../../');
    final buildDir = input.outputDirectory.resolve('cmake/');

    final defines = <String, String?>{
      'SILLOK_BUILD_CLI': 'OFF',
      'SILLOK_BUILD_TESTS': 'OFF',
      if (os == OS.iOS || os == OS.macOS) 'SILLOK_COREML': 'ON',
      // Phones: ARMv8.2 with dot product and fp16 (the app checks the CPU).
      // x86_64 Android is only the emulator, which runs the host's AVX2.
      if (os == OS.android && code.targetArchitecture == Architecture.arm64)
        'GGML_CPU_ARM_ARCH': 'armv8.2-a+dotprod+fp16',
      if (os == OS.android && code.targetArchitecture == Architecture.x64)
        for (final feature in ['GGML_SSE42', 'GGML_AVX', 'GGML_AVX2', 'GGML_FMA', 'GGML_F16C', 'GGML_BMI2']) feature: 'ON',
      // Windows: AVX2 rather than the build machine's CPU, and the C runtime
      // inside the DLLs.
      if (os == OS.windows) ...{
        'GGML_NATIVE': 'OFF',
        'CMAKE_MSVC_RUNTIME_LIBRARY': 'MultiThreaded',
        ...?_vulkan(input, logger),
      },
    };

    await CMakeBuilder.create(
      name: 'sillok',
      sourceDir: root,
      outDir: buildDir,
      defines: defines,
      targets: ['sillok'],
      generator: Generator.ninja,
      parallelUseAllProcessors: true,
      logger: logger,
    ).run(input: input, output: output, logger: logger);

    final dir = Directory.fromUri(buildDir);
    final ffmpeg = Directory.fromUri(buildDir.resolve('third_party/ffmpeg/install/'));
    void add(String name, File file) {
      if (!file.existsSync()) throw StateError('${file.path} was not built');
      output.assets.code.add(CodeAsset(
        package: input.packageName,
        name: name,
        linkMode: DynamicLoadingBundled(),
        file: file.uri,
      ));
    }

    // The library the app binds to (lib/engine/native.dart), then FFmpeg's,
    // which it loads. Windows looks for those next to sillok.dll, so they
    // are copied there.
    add('engine/native.dart', _library(dir, 'sillok', os));
    for (final lib in ['avformat', 'avcodec', 'swresample', 'avutil']) {
      var file = _library(Directory('${ffmpeg.path}${os == OS.windows ? 'bin' : 'lib'}'), lib, os);
      if (os == OS.windows) file = file.copySync('${dir.path}${file.uri.pathSegments.last}');
      add('ffmpeg/$lib', file);
    }

    // Rebuild when the library's sources change.
    for (final path in ['CMakeLists.txt', 'cmake', 'include', 'src', 'patches']) {
      final entity = FileSystemEntity.typeSync(root.resolve(path).toFilePath());
      if (entity == FileSystemEntityType.file) output.dependencies.add(root.resolve(path));
      if (entity != FileSystemEntityType.directory) continue;
      for (final file in Directory.fromUri(root.resolve(path)).listSync(recursive: true)) {
        if (file is File) output.dependencies.add(file.uri);
      }
    }
  });
}

/// The built shared library [name] in [dir]: the file itself, not a link.
File _library(Directory dir, String name, OS os) {
  final pattern = switch (os) {
    OS.windows => RegExp('^(lib)?$name(-[0-9]+)?\\.dll\$'),
    OS.macOS || OS.iOS => RegExp('^lib$name(\\.[0-9]+)*\\.dylib\$'),
    _ => RegExp('^lib$name\\.so(\\.[0-9]+)*\$'),
  };
  final files = dir
      .listSync()
      .whereType<File>()
      .where((f) => pattern.hasMatch(f.uri.pathSegments.last) && FileSystemEntity.typeSync(f.path, followLinks: false) == FileSystemEntityType.file)
      .toList();
  if (files.isEmpty) throw StateError('lib$name not found in ${dir.path}');
  return files.first;
}

/// CMake hints for the Vulkan SDK, or null to build without Vulkan.
Map<String, String>? _vulkan(BuildInput input, Logger logger) {
  var sdk = input.userDefines.path('vulkan_sdk')?.toFilePath();
  if (sdk == null) {
    final installed = Directory(r'C:\VulkanSDK');
    if (installed.existsSync()) {
      final versions = installed.listSync().whereType<Directory>().map((d) => d.path).toList()..sort();
      if (versions.isNotEmpty) sdk = versions.last;
    }
  }
  if (sdk == null) {
    logger.warning('No Vulkan SDK in C:\\VulkanSDK: building without GPU support.');
    return null;
  }
  return {
    'SILLOK_VULKAN': 'ON',
    'Vulkan_INCLUDE_DIR': '$sdk/Include',
    'Vulkan_LIBRARY': '$sdk/Lib/vulkan-1.lib',
    'Vulkan_GLSLC_EXECUTABLE': '$sdk/Bin/glslc.exe',
  };
}
