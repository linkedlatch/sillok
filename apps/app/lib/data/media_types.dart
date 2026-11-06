// The file picker's filter for recordings: audio and video as each system
// classifies them, so the app keeps no list of formats of its own.
import 'dart:ffi';
import 'dart:io';

import 'package:ffi/ffi.dart';
import 'package:file_selector/file_selector.dart';

/// Audio and video files: uniform types on macOS, and on Windows the
/// extensions Windows perceives as audio or video (every file if it reports
/// none).
List<XTypeGroup> mediaTypes(String label) {
  final extensions = Platform.isWindows ? windowsMediaExtensions() : null;
  if (extensions != null && extensions.isEmpty) return const [];
  return [
    XTypeGroup(label: label, uniformTypeIdentifiers: const ['public.audio', 'public.movie'], extensions: extensions),
  ];
}

List<String>? _windowsExtensions;

/// The registered extensions (without the dot) whose perceived type is audio
/// or video, the classification Explorer uses for its kinds.
List<String> windowsMediaExtensions() => _windowsExtensions ??= _readWindowsExtensions();

typedef _RegEnumKeyExNative = Int32 Function(
  IntPtr key,
  Uint32 index,
  Pointer<Utf16> name,
  Pointer<Uint32> nameLength,
  Pointer<Void> reserved,
  Pointer<Utf16> className,
  Pointer<Uint32> classNameLength,
  Pointer<Void> lastWriteTime,
);
typedef _RegEnumKeyEx = int Function(
  int key,
  int index,
  Pointer<Utf16> name,
  Pointer<Uint32> nameLength,
  Pointer<Void> reserved,
  Pointer<Utf16> className,
  Pointer<Uint32> classNameLength,
  Pointer<Void> lastWriteTime,
);
typedef _AssocGetPerceivedTypeNative = Int32 Function(
  Pointer<Utf16> extension,
  Pointer<Int32> type,
  Pointer<Uint32> flags,
  Pointer<Pointer<Utf16>> name,
);
typedef _AssocGetPerceivedType = int Function(
  Pointer<Utf16> extension,
  Pointer<Int32> type,
  Pointer<Uint32> flags,
  Pointer<Pointer<Utf16>> name,
);

List<String> _readWindowsExtensions() {
  final enumKey = DynamicLibrary.open('advapi32.dll')
      .lookupFunction<_RegEnumKeyExNative, _RegEnumKeyEx>('RegEnumKeyExW');
  final perceivedType = DynamicLibrary.open('shlwapi.dll')
      .lookupFunction<_AssocGetPerceivedTypeNative, _AssocGetPerceivedType>('AssocGetPerceivedType');
  // HKEY_CLASSES_ROOT, sign-extended as a handle; PERCEIVED_TYPE_AUDIO and
  // PERCEIVED_TYPE_VIDEO; registry key names are at most 255 characters.
  const classesRoot = -0x80000000;
  const audio = 3;
  const video = 4;
  const nameCapacity = 256;
  const errorNoMoreItems = 259;
  return using((arena) {
    final name = arena<Uint16>(nameCapacity).cast<Utf16>();
    final nameLength = arena<Uint32>();
    final type = arena<Int32>();
    final flags = arena<Uint32>();
    final extensions = <String>[];
    for (var index = 0; ; index++) {
      nameLength.value = nameCapacity;
      final result = enumKey(classesRoot, index, name, nameLength, nullptr, nullptr, nullptr, nullptr);
      if (result == errorNoMoreItems) break;
      if (result != 0) continue;
      final key = name.toDartString(length: nameLength.value);
      if (key.length < 2 || !key.startsWith('.')) continue;
      if (perceivedType(name, type, flags, nullptr) != 0) continue;
      if (type.value == audio || type.value == video) extensions.add(key.substring(1).toLowerCase());
    }
    return extensions;
  });
}
