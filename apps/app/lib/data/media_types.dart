// The file picker's filter for recordings, and the same test for dropped
// files: audio and video as each system classifies them, so the app keeps no
// list of formats of its own.
import 'dart:ffi';
import 'dart:io';

import 'package:ffi/ffi.dart';
import 'package:file_selector/file_selector.dart';

/// Audio and video files: MIME types on Android, uniform types on Apple, and
/// on Windows the extensions Windows perceives as audio or video (every file
/// if it reports none).
List<XTypeGroup> mediaTypes(String label) {
  final extensions = Platform.isWindows ? windowsMediaExtensions() : null;
  if (extensions != null && extensions.isEmpty) return const [];
  return [
    XTypeGroup(
      label: label,
      mimeTypes: const ['audio/*', 'video/*'],
      uniformTypeIdentifiers: const ['public.audio', 'public.movie'],
      extensions: extensions,
    ),
  ];
}

/// Whether the system classifies [path] as audio or video by its extension,
/// as the file picker does; true where the app does not ask.
bool isMediaFile(String path) {
  final name = path.substring(path.lastIndexOf(Platform.isWindows ? RegExp(r'[\\/]') : '/') + 1);
  final dot = name.lastIndexOf('.');
  if (dot < 0) return false;
  final extension = name.substring(dot + 1).toLowerCase();
  if (Platform.isWindows) return windowsMediaExtensions().contains(extension);
  if (Platform.isMacOS) return _appleMediaType(extension);
  return true;
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

typedef _CFStringCreateNative = Pointer<Void> Function(Pointer<Void> allocator, Pointer<Utf8> string, Uint32 encoding);
typedef _CFStringCreate = Pointer<Void> Function(Pointer<Void> allocator, Pointer<Utf8> string, int encoding);
typedef _CFReleaseNative = Void Function(Pointer<Void> object);
typedef _CFRelease = void Function(Pointer<Void> object);
typedef _UTTypeCreateNative = Pointer<Void> Function(
  Pointer<Void> tagClass,
  Pointer<Void> tag,
  Pointer<Void> conformingTo,
);
typedef _UTTypeConformsToNative = Uint8 Function(Pointer<Void> type, Pointer<Void> conformsTo);
typedef _UTTypeConformsTo = int Function(Pointer<Void> type, Pointer<Void> conformsTo);

/// Whether the uniform type of [extension] is audio or video (public.audio,
/// public.movie), the types the picker accepts on Apple.
bool _appleMediaType(String extension) {
  final foundation = DynamicLibrary.open('/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation');
  final services = DynamicLibrary.open('/System/Library/Frameworks/CoreServices.framework/CoreServices');
  final createString = foundation.lookupFunction<_CFStringCreateNative, _CFStringCreate>('CFStringCreateWithCString');
  final release = foundation.lookupFunction<_CFReleaseNative, _CFRelease>('CFRelease');
  final typeOf = services.lookupFunction<_UTTypeCreateNative, _UTTypeCreateNative>(
    'UTTypeCreatePreferredIdentifierForTag',
  );
  final conforms = services.lookupFunction<_UTTypeConformsToNative, _UTTypeConformsTo>('UTTypeConformsTo');
  const utf8 = 0x08000100; // kCFStringEncodingUTF8
  return using((arena) {
    final strings = <Pointer<Void>>[];
    Pointer<Void> string(String value) {
      final created = createString(nullptr, value.toNativeUtf8(allocator: arena), utf8);
      strings.add(created);
      return created;
    }

    try {
      // kUTTagClassFilenameExtension's value.
      final type = typeOf(string('public.filename-extension'), string(extension), nullptr);
      if (type == nullptr) return false;
      strings.add(type);
      return conforms(type, string('public.audio')) != 0 || conforms(type, string('public.movie')) != 0;
    } finally {
      strings.where((s) => s != nullptr).forEach(release);
    }
  });
}
