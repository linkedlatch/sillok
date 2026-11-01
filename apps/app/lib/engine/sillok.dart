// The library's catalog, model checks, devices and exports, over [Native].
import 'dart:convert';

import 'package:ffi/ffi.dart';

import 'native.dart';

/// A file of a catalog model; [zip] files are optional accelerators (the Core
/// ML encoder on Apple) that the app extracts.
class ModelFile {
  const ModelFile({required this.file, required this.url, required this.size, required this.sha256, required this.zip});

  factory ModelFile.fromJson(Map<String, dynamic> json) => ModelFile(
        file: json['file'] as String,
        url: json['url'] as String,
        size: (json['size'] as num).toInt(),
        sha256: json['sha256'] as String,
        zip: json['zip'] as bool,
      );

  final String file;
  final String url;
  final int size;
  final String sha256;
  final bool zip;
}

/// A model of the catalog: "voice_detection", "transcription" or
/// "diarization", described by [family] and [quantization].
class CatalogModel {
  const CatalogModel({
    required this.id,
    required this.role,
    required this.family,
    required this.quantization,
    required this.recommended,
    required this.files,
  });

  factory CatalogModel.fromJson(Map<String, dynamic> json) => CatalogModel(
        id: json['id'] as String,
        role: json['role'] as String,
        family: json['family'] as String,
        quantization: json['quantization'] as String,
        recommended: json['recommended'] as bool,
        files: [for (final file in json['files'] as List) ModelFile.fromJson(file as Map<String, dynamic>)],
      );

  final String id;
  final String role;
  final String family;
  final String quantization;
  final bool recommended;
  final List<ModelFile> files;

  bool get isTranscription => role == 'transcription';

  /// Bytes to download: the files the library uses, not the archives.
  int get size => files.where((f) => !f.zip).fold(0, (sum, f) => sum + f.size);
}

/// A compute device the library can use; [type] is "cpu", "gpu", "igpu", ...
class ComputeDevice {
  const ComputeDevice({required this.name, required this.description, required this.type, required this.memoryTotal});

  final String name;
  final String description;
  final String type;
  final int memoryTotal;

  bool get isGpu => type == 'gpu' || type == 'igpu';
}

/// State of one catalog file in a models directory: "ok", "missing",
/// "wrong_size", "wrong_hash" or, for archives, "extract".
class FileStatus {
  const FileStatus({required this.model, required this.file, required this.status});

  final String model;
  final String file;
  final String status;
}

abstract final class Sillok {
  static Native get _native => Native.instance;

  static String get version => _native.version().toDartString();

  static List<CatalogModel> catalog() => [
        for (final model in jsonDecode(_native.takeString(_native.modelCatalog)) as List)
          CatalogModel.fromJson(model as Map<String, dynamic>),
      ];

  /// The models an engine with [transcriptionModel] (null: the recommended
  /// one) uses, one per role.
  static List<CatalogModel> engineModels(String? transcriptionModel) =>
      withUtf8(transcriptionModel ?? '', (id) => [
            for (final model in jsonDecode(_native.takeString((out) => _native.engineModels(id, out))) as List)
              CatalogModel.fromJson(model as Map<String, dynamic>),
          ]);

  /// Checks the files in [modelsDir]; [verifyHash] reads every file.
  static List<FileStatus> check(String modelsDir, {required bool verifyHash}) =>
      withUtf8(modelsDir, (dir) => [
            for (final entry
                in jsonDecode(_native.takeString((out) => _native.modelCheck(dir, verifyHash ? 1 : 0, out))) as List)
              FileStatus(
                model: entry['model'] as String,
                file: entry['file'] as String,
                status: entry['status'] as String,
              ),
          ]);

  static List<ComputeDevice> devices() => [
        for (final device in jsonDecode(_native.takeString(_native.devices)) as List)
          ComputeDevice(
            name: device['name'] as String,
            description: device['description'] as String,
            type: device['type'] as String,
            memoryTotal: (device['memory_total'] as num).toInt(),
          ),
      ];

  /// The transcript as "txt", "srt" or "json"; [speakerLabel] names unnamed
  /// speakers, with {n} for the number.
  static String export(String transcriptJson, String format, String speakerLabel) =>
      withUtf8(transcriptJson, (transcript) => withUtf8(format, (kind) => withUtf8(
            jsonEncode({'speaker_label': speakerLabel}),
            (options) => _native.takeString((out) => _native.export(transcript, kind, options, out)),
          )));
}
