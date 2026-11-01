// The C ABI of the sillok library (include/sillok/sillok.h), one Dart
// function per C function. Strings are UTF-8; strings the library returns
// are released with sillok_free_string.
import 'dart:ffi';

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

// The library is the code asset of this file (hook/build.dart builds it), so
// every platform finds it wherever Flutter bundled it.

@Native<Pointer<Utf8> Function()>(symbol: 'sillok_version')
external Pointer<Utf8> _version();

@Native<Pointer<Utf8> Function()>(symbol: 'sillok_last_error')
external Pointer<Utf8> _lastError();

@Native<Void Function(Pointer<Utf8>)>(symbol: 'sillok_free_string')
external void _freeString(Pointer<Utf8> text);

@Native<Int32 Function(Pointer<Pointer<Utf8>>)>(symbol: 'sillok_model_catalog')
external int _modelCatalog(Pointer<Pointer<Utf8>> out);

@Native<Int32 Function(Pointer<Utf8>, Pointer<Pointer<Utf8>>)>(symbol: 'sillok_engine_models')
external int _engineModels(Pointer<Utf8> id, Pointer<Pointer<Utf8>> out);

@Native<Int32 Function(Pointer<Utf8>, Int32, Pointer<Pointer<Utf8>>)>(symbol: 'sillok_model_check')
external int _modelCheck(Pointer<Utf8> dir, int verifyHash, Pointer<Pointer<Utf8>> out);

@Native<Int32 Function(Pointer<Pointer<Utf8>>)>(symbol: 'sillok_devices')
external int _devices(Pointer<Pointer<Utf8>> out);

@Native<Int32 Function(Pointer<Utf8>, Pointer<Pointer<SillokEngine>>)>(symbol: 'sillok_engine_create')
external int _engineCreate(Pointer<Utf8> config, Pointer<Pointer<SillokEngine>> out);

@Native<Void Function(Pointer<SillokEngine>)>(symbol: 'sillok_engine_destroy')
external void _engineDestroy(Pointer<SillokEngine> engine);

@Native<Int32 Function(Pointer<SillokEngine>, Pointer<Utf8>, Pointer<Pointer<SillokJob>>)>(symbol: 'sillok_job_create')
external int _jobCreate(Pointer<SillokEngine> engine, Pointer<Utf8> options, Pointer<Pointer<SillokJob>> out);

@Native<Int32 Function(Pointer<SillokJob>, Pointer<Utf8>, Pointer<NativeFunction<FileProgressFn>>, Pointer<Void>)>(symbol: 'sillok_job_push_file')
external int _jobPushFile(Pointer<SillokJob> job, Pointer<Utf8> path, Pointer<NativeFunction<FileProgressFn>> progress, Pointer<Void> user);

@Native<Int32 Function(Pointer<SillokJob>)>(symbol: 'sillok_job_end_audio')
external int _jobEndAudio(Pointer<SillokJob> job);

@Native<Int32 Function(Pointer<SillokJob>, Pointer<Utf8>, Pointer<NativeFunction<FileProgressFn>>, Pointer<Void>)>(symbol: 'sillok_job_save_audio')
external int _jobSaveAudio(Pointer<SillokJob> job, Pointer<Utf8> path, Pointer<NativeFunction<FileProgressFn>> progress, Pointer<Void> user);

@Native<Int32 Function(Pointer<SillokJob>, Pointer<NativeFunction<SegmentsFn>>, Pointer<Void>)>(symbol: 'sillok_job_on_segments')
external int _jobOnSegments(Pointer<SillokJob> job, Pointer<NativeFunction<SegmentsFn>> callback, Pointer<Void> user);

@Native<Int32 Function(Pointer<SillokJob>, Pointer<NativeFunction<ProgressFn>>, Pointer<Void>, Pointer<Pointer<Utf8>>)>(symbol: 'sillok_job_run')
external int _jobRun(Pointer<SillokJob> job, Pointer<NativeFunction<ProgressFn>> progress, Pointer<Void> user, Pointer<Pointer<Utf8>> out);

@Native<Void Function(Pointer<SillokJob>)>(symbol: 'sillok_job_cancel')
external void _jobCancel(Pointer<SillokJob> job);

@Native<Void Function(Pointer<SillokJob>)>(symbol: 'sillok_job_destroy')
external void _jobDestroy(Pointer<SillokJob> job);

@Native<Int32 Function(Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Pointer<Utf8>>)>(symbol: 'sillok_export')
external int _export(Pointer<Utf8> transcript, Pointer<Utf8> format, Pointer<Utf8> options, Pointer<Pointer<Utf8>> out);

/// The C functions as methods, so that callers can pass them around.
final class NativeApi {
  const NativeApi._();

  static const NativeApi instance = NativeApi._();

  Pointer<Utf8> version() => _version();
  Pointer<Utf8> lastError() => _lastError();
  void freeString(Pointer<Utf8> text) => _freeString(text);
  int modelCatalog(Pointer<Pointer<Utf8>> out) => _modelCatalog(out);
  int engineModels(Pointer<Utf8> id, Pointer<Pointer<Utf8>> out) => _engineModels(id, out);
  int modelCheck(Pointer<Utf8> dir, int verifyHash, Pointer<Pointer<Utf8>> out) => _modelCheck(dir, verifyHash, out);
  int devices(Pointer<Pointer<Utf8>> out) => _devices(out);
  int engineCreate(Pointer<Utf8> config, Pointer<Pointer<SillokEngine>> out) => _engineCreate(config, out);
  void engineDestroy(Pointer<SillokEngine> engine) => _engineDestroy(engine);
  int jobCreate(Pointer<SillokEngine> engine, Pointer<Utf8> options, Pointer<Pointer<SillokJob>> out) => _jobCreate(engine, options, out);
  int jobPushFile(Pointer<SillokJob> job, Pointer<Utf8> path, Pointer<NativeFunction<FileProgressFn>> progress, Pointer<Void> user) => _jobPushFile(job, path, progress, user);
  int jobEndAudio(Pointer<SillokJob> job) => _jobEndAudio(job);
  int jobSaveAudio(Pointer<SillokJob> job, Pointer<Utf8> path, Pointer<NativeFunction<FileProgressFn>> progress, Pointer<Void> user) => _jobSaveAudio(job, path, progress, user);
  int jobOnSegments(Pointer<SillokJob> job, Pointer<NativeFunction<SegmentsFn>> callback, Pointer<Void> user) => _jobOnSegments(job, callback, user);
  int jobRun(Pointer<SillokJob> job, Pointer<NativeFunction<ProgressFn>> progress, Pointer<Void> user, Pointer<Pointer<Utf8>> out) => _jobRun(job, progress, user, out);
  void jobCancel(Pointer<SillokJob> job) => _jobCancel(job);
  void jobDestroy(Pointer<SillokJob> job) => _jobDestroy(job);
  int export(Pointer<Utf8> transcript, Pointer<Utf8> format, Pointer<Utf8> options, Pointer<Pointer<Utf8>> out) => _export(transcript, format, options, out);

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
