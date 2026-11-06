// Builds the sillok library (the repository's CMake project, with FFmpeg)
// for the platform the app is built for, and bundles it with FFmpeg's
// libraries as code assets. Flutter places them in each platform's app and
// fixes the references between them where needed (Apple frameworks).
//
// CMake and Ninja come from the PATH; the compilers are the ones Flutter
// names for the target: Visual Studio's (through its developer environment)
// for Windows, Xcode's for macOS. The CMake arguments for each target are
// those of the core workflow's builds.
//
// Windows builds need MSYS2's make for FFmpeg (README, Build), and use
// Vulkan when its SDK is found: hooks see only a few environment variables,
// so the hook looks in C:\VulkanSDK unless the pubspec names the SDK
// (hooks: user_defines: sillok: vulkan_sdk: <path>).
import 'dart:io';

import 'package:code_assets/code_assets.dart';
import 'package:hooks/hooks.dart';

void main(List<String> args) async {
  await build(args, (input, output) async {
    if (!input.config.buildCodeAssets) return;
    final code = input.config.code;
    final os = code.targetOS;
    final root = input.packageRoot.resolve('../../');
    // Windows paths stop at 260 characters, which the nested builds inside
    // the hook's own directory pass; there the build goes to a short one.
    final localAppData = Platform.environment['LOCALAPPDATA'];
    final buildDir = os == OS.windows && Platform.isWindows && localAppData != null
        ? Directory(
            '$localAppData\\sillok\\build\\${input.outputDirectory.pathSegments.lastWhere((s) => s.isNotEmpty)}\\',
          ).uri
        : input.outputDirectory.resolve('cmake/');

    final defines = <String, String>{
      'SILLOK_BUILD_CLI': 'OFF',
      'SILLOK_BUILD_TESTS': 'OFF',
      // Stated every time, so a build folder that once had it on (in its
      // CMake cache) does not keep it; Windows turns it on below.
      'SILLOK_VULKAN': 'OFF',
      // Core ML for the NPU encoder, on Apple silicon's Neural Engine. Stated
      // every time, as above.
      'SILLOK_COREML': os == OS.macOS ? 'ON' : 'OFF',
      // macOS: the default CPU of the target (Apple M1) rather than the
      // build machine's.
      if (os == OS.macOS) 'GGML_NATIVE': 'OFF',
      // Windows: AVX2 rather than the build machine's CPU, and the C runtime
      // inside the DLLs.
      if (os == OS.windows) ...{
        'GGML_NATIVE': 'OFF',
        'CMAKE_MSVC_RUNTIME_LIBRARY': 'MultiThreaded',
        ...?_vulkan(input),
      },
    };

    await _cmake(code, root, buildDir, {..._target(code), ...defines});

    final dir = Directory.fromUri(buildDir);
    final ffmpeg = Directory.fromUri(buildDir.resolve('third_party/ffmpeg/install/'));
    void add(String name, File file) {
      if (!file.existsSync()) throw StateError('${file.path} was not built');
      output.assets.code.add(
        CodeAsset(package: input.packageName, name: name, linkMode: DynamicLoadingBundled(), file: file.uri),
      );
    }

    // The library the app binds to (lib/engine/native.dart), then FFmpeg's,
    // which it loads. On Windows all go into one folder of the hook's own,
    // since Windows looks for FFmpeg's DLLs next to sillok.dll and the build
    // folder is elsewhere.
    final bin = Directory.fromUri(input.outputDirectory.resolve('bin/'));
    File place(File file) {
      if (os != OS.windows) return file;
      bin.createSync(recursive: true);
      return file.copySync('${bin.path}${file.uri.pathSegments.last}');
    }

    add('engine/native.dart', place(_library(dir, 'sillok', os)));
    for (final lib in ['avformat', 'avcodec', 'avfilter', 'swresample', 'avutil']) {
      add('ffmpeg/$lib', place(_library(Directory('${ffmpeg.path}${os == OS.windows ? 'bin' : 'lib'}'), lib, os)));
    }

    // Rebuild when the library's sources change. Directories count too, so an
    // added file is noticed, not only edits to known ones.
    for (final path in ['CMakeLists.txt', 'cmake', 'include', 'src']) {
      final entity = FileSystemEntity.typeSync(root.resolve(path).toFilePath());
      if (entity == FileSystemEntityType.file) output.dependencies.add(root.resolve(path));
      if (entity != FileSystemEntityType.directory) continue;
      final sources = Directory.fromUri(root.resolve('$path/'));
      output.dependencies.add(sources.uri);
      for (final entry in sources.listSync(recursive: true)) {
        output.dependencies.add(entry.uri);
      }
    }
  });
}

/// The built shared library [name] in [dir]: the file itself, not a link.
File _library(Directory dir, String name, OS os) {
  final pattern = switch (os) {
    OS.windows => RegExp('^(lib)?$name(-[0-9]+)?\\.dll\$'),
    OS.macOS => RegExp('^lib$name(\\.[0-9]+)*\\.dylib\$'),
    _ => RegExp('^lib$name\\.so(\\.[0-9]+)*\$'),
  };
  final files = dir
      .listSync()
      .whereType<File>()
      .where(
        (f) =>
            pattern.hasMatch(f.uri.pathSegments.last) &&
            FileSystemEntity.typeSync(f.path, followLinks: false) == FileSystemEntityType.file,
      )
      .toList();
  if (files.isEmpty) throw StateError('lib$name not found in ${dir.path}');
  return files.first;
}

/// Configures the CMake project [root] in [buildDir] and builds the library.
/// The tools' output shows only when a step fails, with the step.
Future<void> _cmake(CodeConfig code, Uri root, Uri buildDir, Map<String, String> defines) async {
  // Visual Studio's developer environment puts its own CMake and Ninja first
  // on the PATH; the ones on the PATH the build started with are the
  // project's (README, Build).
  final cmake = _onPath('cmake');
  final ninja = _onPath('ninja');
  final environment = code.targetOS == OS.windows ? await _developerEnvironment(code) : null;
  Future<void> run(List<String> arguments) async {
    final result = await Process.run(cmake, arguments, environment: environment);
    if (result.exitCode == 0) return;
    stderr
      ..writeln('cmake ${arguments.join(' ')}')
      ..writeln(result.stdout)
      ..writeln(result.stderr);
    throw ProcessException(cmake, arguments, 'exit code ${result.exitCode}', result.exitCode);
  }

  final dir = buildDir.toFilePath();
  await run([
    '-S',
    root.toFilePath(),
    '-B',
    dir,
    '-G',
    'Ninja',
    '-DCMAKE_MAKE_PROGRAM=$ninja',
    '-DCMAKE_BUILD_TYPE=Release',
    for (final MapEntry(:key, :value) in defines.entries) '-D$key=$value',
  ]);
  await run(['--build', dir, '--target', 'sillok', '--parallel', '${Platform.numberOfProcessors}']);
}

/// The CMake variables that choose the target platform and its compilers.
Map<String, String> _target(CodeConfig code) {
  if (code.targetOS != OS.macOS) return {};
  if (code.targetArchitecture != Architecture.arm64) {
    throw UnsupportedError('${code.targetArchitecture} on macOS: only Apple silicon is supported');
  }
  return {
    'CMAKE_OSX_SYSROOT': 'macosx',
    'CMAKE_OSX_ARCHITECTURES': 'arm64',
    'CMAKE_OSX_DEPLOYMENT_TARGET': '${code.macOS.targetVersion}',
  };
}

/// The full path of [tool] on the PATH.
String _onPath(String tool) {
  final windows = Platform.isWindows;
  for (final dir in (Platform.environment['PATH'] ?? '').split(windows ? ';' : ':')) {
    if (dir.isEmpty) continue;
    final file = File('$dir${Platform.pathSeparator}$tool${windows ? '.exe' : ''}');
    if (file.existsSync()) return file.path;
  }
  throw StateError('$tool is not on the PATH');
}

/// The environment of Visual Studio's developer prompt, which Flutter names:
/// the compiler, its headers and libraries.
Future<Map<String, String>> _developerEnvironment(CodeConfig code) async {
  final prompt = code.cCompiler?.windows.developerCommandPrompt;
  if (prompt == null) throw StateError('Flutter found no Visual Studio C++ tools');
  // From the script's own folder, so that cmd needs no quotes around its path.
  final script = File.fromUri(prompt.script);
  final result = await Process.run(
    '${script.uri.pathSegments.last} ${prompt.arguments.join(' ')} > nul && set',
    const [],
    runInShell: true,
    workingDirectory: script.parent.path,
  );
  if (result.exitCode != 0) throw StateError('${script.path} failed: ${result.stdout}${result.stderr}');
  return {
    for (final line in (result.stdout as String).split(RegExp(r'\r?\n')))
      if (line.indexOf('=') case final i when i > 0) line.substring(0, i): line.substring(i + 1),
  };
}

/// CMake hints for the Vulkan SDK, or null to build without Vulkan.
Map<String, String>? _vulkan(BuildInput input) {
  var sdk = input.userDefines.path('vulkan_sdk')?.toFilePath();
  if (sdk == null) {
    final installed = Directory(r'C:\VulkanSDK');
    if (installed.existsSync()) {
      final versions = installed.listSync().whereType<Directory>().map((d) => d.path).toList()..sort();
      if (versions.isNotEmpty) sdk = versions.last;
    }
  }
  if (sdk == null) {
    stderr.writeln('No Vulkan SDK in C:\\VulkanSDK: building without GPU support.');
    return null;
  }
  return {
    'SILLOK_VULKAN': 'ON',
    // The SDK's CMake packages (SPIRV-Headers), found through VULKAN_SDK otherwise.
    'CMAKE_PREFIX_PATH': sdk,
    'Vulkan_INCLUDE_DIR': '$sdk/Include',
    'Vulkan_LIBRARY': '$sdk/Lib/vulkan-1.lib',
    'Vulkan_GLSLC_EXECUTABLE': '$sdk/Bin/glslc.exe',
  };
}
