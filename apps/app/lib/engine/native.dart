// The C ABI of the sillok library (include/sillok/sillok.h), one Dart
// function per C function. Strings are UTF-8; strings the library returns
// are released with sillok_free_string. Each isolate opens the library for
// itself.
import 'dart:ffi';
import 'dart:io';

import 'package:ffi/ffi.dart';

final class SillokEngine extends Opaque {}

final class SillokJob extends Opaque {}

typedef ProgressFn = Void Function(Pointer<Void>, Int32, Double);
typedef FileProgressFn = Void Function(Pointer<Void>, Double);
typedef SegmentsFn = Void Function(Pointer<Void>, Pointer<Utf8>);

/// Status codes of sillok_status.
abstract final class Status {
  static const ok = 0;
  static const invalidArgument = 1;
  static const modelError = 2;
  static const cancelled = 3;
  static const busy = 4;
  static const internalError = 5;
  static const mediaError = 6;
}

/// A failed call: the status and sillok_last_error().
class SillokException implements Exception {
  SillokException(this.status, this.message);

  final int status;
  final String message;

  @override
  String toString() => message;
}

/// Where the library is: SILLOK_LIBRARY when set (tests, development),
/// otherwise next to the app.
DynamicLibrary _open() {
  final path = Platform.environment['SILLOK_LIBRARY'];
  if (path != null && path.isNotEmpty) return DynamicLibrary.open(path);
  if (Platform.isIOS) return DynamicLibrary.process();
  if (Platform.isWindows) return DynamicLibrary.open('sillok.dll');
  if (Platform.isMacOS) return DynamicLibrary.open('libsillok.dylib');
  return DynamicLibrary.open('libsillok.so');
}

final class Native {
  Native._(DynamicLibrary lib)
      : version = lib.lookupFunction<Pointer<Utf8> Function(), Pointer<Utf8> Function()>(
            'sillok_version'),
        lastError = lib.lookupFunction<Pointer<Utf8> Function(), Pointer<Utf8> Function()>(
            'sillok_last_error'),
        freeString = lib.lookupFunction<Void Function(Pointer<Utf8>), void Function(Pointer<Utf8>)>(
            'sillok_free_string'),
        modelCatalog = lib.lookupFunction<Int32 Function(Pointer<Pointer<Utf8>>),
            int Function(Pointer<Pointer<Utf8>>)>('sillok_model_catalog'),
        engineModels = lib.lookupFunction<Int32 Function(Pointer<Utf8>, Pointer<Pointer<Utf8>>),
            int Function(Pointer<Utf8>, Pointer<Pointer<Utf8>>)>('sillok_engine_models'),
        modelCheck = lib.lookupFunction<Int32 Function(Pointer<Utf8>, Int32, Pointer<Pointer<Utf8>>),
            int Function(Pointer<Utf8>, int, Pointer<Pointer<Utf8>>)>('sillok_model_check'),
        devices = lib.lookupFunction<Int32 Function(Pointer<Pointer<Utf8>>),
            int Function(Pointer<Pointer<Utf8>>)>('sillok_devices'),
        engineCreate = lib.lookupFunction<Int32 Function(Pointer<Utf8>, Pointer<Pointer<SillokEngine>>),
            int Function(Pointer<Utf8>, Pointer<Pointer<SillokEngine>>)>('sillok_engine_create'),
        engineDestroy = lib.lookupFunction<Void Function(Pointer<SillokEngine>),
            void Function(Pointer<SillokEngine>)>('sillok_engine_destroy'),
        jobCreate = lib.lookupFunction<
            Int32 Function(Pointer<SillokEngine>, Pointer<Utf8>, Pointer<Pointer<SillokJob>>),
            int Function(Pointer<SillokEngine>, Pointer<Utf8>, Pointer<Pointer<SillokJob>>)>('sillok_job_create'),
        jobPushFile = lib.lookupFunction<
            Int32 Function(Pointer<SillokJob>, Pointer<Utf8>, Pointer<NativeFunction<FileProgressFn>>, Pointer<Void>),
            int Function(Pointer<SillokJob>, Pointer<Utf8>, Pointer<NativeFunction<FileProgressFn>>,
                Pointer<Void>)>('sillok_job_push_file'),
        jobEndAudio = lib.lookupFunction<Int32 Function(Pointer<SillokJob>), int Function(Pointer<SillokJob>)>(
            'sillok_job_end_audio'),
        jobSaveAudio = lib.lookupFunction<
            Int32 Function(Pointer<SillokJob>, Pointer<Utf8>, Pointer<NativeFunction<FileProgressFn>>, Pointer<Void>),
            int Function(Pointer<SillokJob>, Pointer<Utf8>, Pointer<NativeFunction<FileProgressFn>>,
                Pointer<Void>)>('sillok_job_save_audio'),
        jobOnSegments = lib.lookupFunction<
            Int32 Function(Pointer<SillokJob>, Pointer<NativeFunction<SegmentsFn>>, Pointer<Void>),
            int Function(Pointer<SillokJob>, Pointer<NativeFunction<SegmentsFn>>, Pointer<Void>)>('sillok_job_on_segments'),
        jobRun = lib.lookupFunction<
            Int32 Function(Pointer<SillokJob>, Pointer<NativeFunction<ProgressFn>>, Pointer<Void>, Pointer<Pointer<Utf8>>),
            int Function(Pointer<SillokJob>, Pointer<NativeFunction<ProgressFn>>, Pointer<Void>,
                Pointer<Pointer<Utf8>>)>('sillok_job_run'),
        jobCancel = lib.lookupFunction<Void Function(Pointer<SillokJob>), void Function(Pointer<SillokJob>)>(
            'sillok_job_cancel'),
        jobDestroy = lib.lookupFunction<Void Function(Pointer<SillokJob>), void Function(Pointer<SillokJob>)>(
            'sillok_job_destroy'),
        export = lib.lookupFunction<
            Int32 Function(Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Pointer<Utf8>>),
            int Function(Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Pointer<Utf8>>)>('sillok_export');

  /// The library, opened once per isolate.
  static final Native instance = Native._(_open());

  final Pointer<Utf8> Function() version;
  final Pointer<Utf8> Function() lastError;
  final void Function(Pointer<Utf8>) freeString;
  final int Function(Pointer<Pointer<Utf8>>) modelCatalog;
  final int Function(Pointer<Utf8>, Pointer<Pointer<Utf8>>) engineModels;
  final int Function(Pointer<Utf8>, int, Pointer<Pointer<Utf8>>) modelCheck;
  final int Function(Pointer<Pointer<Utf8>>) devices;
  final int Function(Pointer<Utf8>, Pointer<Pointer<SillokEngine>>) engineCreate;
  final void Function(Pointer<SillokEngine>) engineDestroy;
  final int Function(Pointer<SillokEngine>, Pointer<Utf8>, Pointer<Pointer<SillokJob>>) jobCreate;
  final int Function(Pointer<SillokJob>, Pointer<Utf8>, Pointer<NativeFunction<FileProgressFn>>, Pointer<Void>)
      jobPushFile;
  final int Function(Pointer<SillokJob>) jobEndAudio;
  final int Function(Pointer<SillokJob>, Pointer<Utf8>, Pointer<NativeFunction<FileProgressFn>>, Pointer<Void>)
      jobSaveAudio;
  final int Function(Pointer<SillokJob>, Pointer<NativeFunction<SegmentsFn>>, Pointer<Void>) jobOnSegments;
  final int Function(Pointer<SillokJob>, Pointer<NativeFunction<ProgressFn>>, Pointer<Void>, Pointer<Pointer<Utf8>>)
      jobRun;
  final void Function(Pointer<SillokJob>) jobCancel;
  final void Function(Pointer<SillokJob>) jobDestroy;
  final int Function(Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Pointer<Utf8>>) export;

  /// Throws [SillokException] unless [status] is OK.
  void check(int status) {
    if (status != Status.ok) throw SillokException(status, lastError().toDartString());
  }

  /// Calls [call] with an out-string pointer and returns the string it set.
  String takeString(int Function(Pointer<Pointer<Utf8>>) call) {
    final out = calloc<Pointer<Utf8>>();
    try {
      check(call(out));
      final text = out.value.toDartString();
      freeString(out.value);
      return text;
    } finally {
      calloc.free(out);
    }
  }
}

/// Runs [body] with [text] as a native UTF-8 string, freed afterwards.
T withUtf8<T>(String text, T Function(Pointer<Utf8>) body) {
  final native = text.toNativeUtf8();
  try {
    return body(native);
  } finally {
    calloc.free(native);
  }
}
