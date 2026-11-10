// What the screens show and do: the models on this device, the defaults for
// new files, the library of finished files and the queue.
import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:isolate';

import 'package:archive/archive_io.dart';
import 'package:flutter/foundation.dart';
import 'package:path_provider/path_provider.dart';

import 'data/downloader.dart';
import 'data/keep_awake.dart';
import 'data/settings.dart';
import 'data/store.dart';
import 'data/transcriber.dart';
import 'engine/job_run.dart';
import 'engine/sillok.dart';
import 'ui/playback.dart';

sealed class ModelStatus {
  const ModelStatus();
}

/// Every file is here.
final class ModelReady extends ModelStatus {
  const ModelReady();
}

/// Some files are missing; [downloaded] bytes are here already.
final class ModelMissing extends ModelStatus {
  const ModelMissing(this.downloaded);

  final int downloaded;
}

final class ModelDownloading extends ModelStatus {
  const ModelDownloading(this.progress);

  final DownloadProgress progress;
}

/// Checking the downloaded files' hashes.
final class ModelVerifying extends ModelStatus {
  const ModelVerifying();
}

/// Messages for the screens to show once.
sealed class Notice {
  const Notice();
}

final class DamagedModels extends Notice {
  const DamagedModels();
}

final class DownloadFailed extends Notice {
  const DownloadFailed(this.message);

  final String message;
}

final class ModelsVerified extends Notice {
  const ModelsVerified();
}

class AppState extends ChangeNotifier {
  late final Directory _support;
  late final Directory modelsDir;
  late final JobStore store;
  late final Transcriber transcriber;
  final _downloader = ModelDownloader();

  bool started = false;
  List<CatalogModel> catalog = const [];
  List<ComputeDevice> gpus = const [];
  Map<String, ModelStatus> models = const {};

  /// Per transcription model with an accelerator (the Core ML encoder on
  /// Apple, as zip files in the catalog): ModelReady once unpacked,
  /// ModelVerifying while unpacking.
  Map<String, ModelStatus> accelerators = const {};
  bool modelsChecked = false;
  String? modelsError;
  JobSettings defaults = const JobSettings();

  /// The language of the app's own text ("ko", "en"), or null for the
  /// system's. Apart from the state's other changes, as the whole app
  /// follows it.
  final appLanguage = ValueNotifier<String?>(null);
  List<LibraryEntry> library = const [];
  Notice? notice;

  /// A player for a result's copy; tests stand in for the platform's.
  Playback Function() newPlayback = AudioPlayback.new;
  String? _downloading;
  final _finished = StreamController<(String key, String id)>.broadcast();

  /// A file finished and its result is in [library]: its queue key and job id.
  Stream<(String key, String id)> get finished => _finished.stream;

  File get _settingsFile => File('${_support.path}${Platform.pathSeparator}settings.json');
  File get _appFile => File('${_support.path}${Platform.pathSeparator}app.json');

  Future<void> start() async {
    _support = await getApplicationSupportDirectory();
    await _loadAppLanguage();
    final cache = await getApplicationCacheDirectory();
    await cache.create(recursive: true);
    modelsDir = Directory('${_support.path}${Platform.pathSeparator}models');
    store = JobStore(Directory('${_support.path}${Platform.pathSeparator}jobs'));
    transcriber = Transcriber(
      store: store,
      queueFile: File('${_support.path}${Platform.pathSeparator}queue.json'),
      keepAwake: KeepAwake.system(),
      request: (item, lane, copyPath, checkpointPath) => JobRequest(
        modelsDir: modelsDir.path,
        tempDir: cache.path,
        gpuIndex: item.settings.gpuIndex,
        npu: item.settings.npu,
        transcriptionModel: item.settings.transcriptionModel,
        options: jobOptions(item.settings, lane, checkpointPath),
        source: item.path,
        copyPath: copyPath,
        prepare: lane == Lane.prepare,
      ),
    );
    transcriber.addListener(notifyListeners);
    transcriber.finished.listen((done) async {
      await _loadLibrary();
      _finished.add(done);
    });

    catalog = Sillok.catalog();
    gpus = Sillok.devices().where((d) => d.isGpu).toList();
    await _loadSettings();
    await _loadLibrary();
    started = true;
    notifyListeners();
    await checkModels();
    await transcriber.resume();
  }

  // --- Models -----------------------------------------------------------------

  List<CatalogModel> get transcriptionModels => catalog.where((m) => m.isTranscription).toList();

  CatalogModel get recommendedTranscription => transcriptionModels.firstWhere((m) => m.recommended);

  List<CatalogModel> get readyTranscriptionModels =>
      transcriptionModels.where((m) => models[m.id] is ModelReady).toList();

  /// The models an engine with [transcriptionModel] uses, one per role.
  List<CatalogModel> engineModels(String? transcriptionModel) => Sillok.engineModels(transcriptionModel);

  /// Whether every model a run with [settings] needs is here.
  bool canRun(JobSettings settings) =>
      engineModels(settings.transcriptionModel).every((m) => models[m.id] is ModelReady);

  /// The first start: no transcription model and nothing transcribed yet.
  bool get needsWelcome => modelsChecked && readyTranscriptionModels.isEmpty && library.isEmpty;

  Future<void> checkModels() async {
    modelsError = null;
    notifyListeners();
    try {
      final dir = modelsDir.path;
      final statuses = await _check(dir, verifyHash: false);
      _setStatuses(statuses);
      modelsChecked = true;
    } on Object catch (error) {
      modelsError = '$error';
    }
    notifyListeners();
  }

  /// Whether the model's accelerator (the NPU encoder) is here, so
  /// that a job's npu setting takes effect.
  bool hasAccelerator(String? modelId) => accelerators[modelId ?? recommendedTranscription.id] is ModelReady;

  void _setStatuses(List<FileStatus> statuses) {
    accelerators = {
      for (final model in catalog)
        if (model.files.any((f) => f.zip))
          model.id: _acceleratorBusy == model.id
              ? accelerators[model.id] ?? const ModelMissing(0)
              : statuses.where((s) => s.model == model.id && _isZip(model, s.file)).every((s) => s.status == 'ok')
              ? const ModelReady()
              : ModelMissing(ModelDownloader.downloaded(modelsDir, model.files.where((f) => f.zip).toList())),
    };
    models = {
      for (final model in catalog)
        model.id: _downloading == model.id && models[model.id] is! ModelReady
            ? models[model.id]!
            : statuses.where((s) => s.model == model.id && !_isZip(model, s.file)).every((s) => s.status == 'ok')
            ? const ModelReady()
            : ModelMissing(ModelDownloader.downloaded(modelsDir, model.files.where((f) => !f.zip).toList())),
    };
  }

  static bool _isZip(CatalogModel model, String file) => model.files.any((f) => f.file == file && f.zip);

  /// The files [downloadModel] fetches for [modelId]: those of the models an
  /// engine with it needs that are not here, then with [accelerator] its
  /// accelerator's (on Apple, the Core ML encoder).
  List<ModelFile> _toDownload(String modelId, {required bool accelerator}) {
    final needed = engineModels(modelId);
    // The models first, so that a stop during the larger encoder leaves them
    // usable.
    return <String, ModelFile>{
      for (final model in needed)
        if (models[model.id] is! ModelReady)
          for (final file in model.files)
            if (!file.zip) file.file: file,
      if (accelerator)
        for (final model in needed)
          if (accelerators[model.id] case final status? when status is! ModelReady)
            for (final file in model.files)
              if (file.zip) file.file: file,
    }.values.toList();
  }

  /// Bytes [downloadModel] fetches for [modelId].
  int downloadSize(String modelId, {required bool accelerator}) =>
      _toDownload(modelId, accelerator: accelerator).fold(0, (sum, f) => sum + f.size);

  /// Bytes of [modelId]'s accelerator still to download; 0 where there is
  /// none (outside Apple) or it is here.
  int acceleratorDownloadSize(String modelId) =>
      downloadSize(modelId, accelerator: true) - downloadSize(modelId, accelerator: false);

  /// Downloads the files of [modelId] and, for the first start, the other
  /// models an engine needs, with [accelerator] its accelerator too; checks
  /// their hashes once all are here and unpacks the accelerator. The first
  /// model here becomes the default.
  Future<void> downloadModel(String modelId, {required bool accelerator}) async {
    if (_downloading != null || _acceleratorBusy != null) return;
    final wanted = {for (final model in engineModels(modelId)) model.id: model};
    final files = _toDownload(modelId, accelerator: accelerator);
    _downloading = modelId;
    void show(ModelStatus status) {
      models = {
        ...models,
        for (final id in wanted.keys)
          if (models[id] is! ModelReady) id: status,
      };
      notifyListeners();
    }

    show(
      ModelDownloading(
        DownloadProgress(ModelDownloader.downloaded(modelsDir, files), files.fold(0, (sum, f) => sum + f.size), 0),
      ),
    );
    try {
      await _downloader.download(modelsDir, files, (progress) => show(ModelDownloading(progress)));
      show(const ModelVerifying());
      final dir = modelsDir.path;
      final statuses = await _check(dir, verifyHash: true);
      final damaged = statuses.where((s) => s.status == 'wrong_hash' || s.status == 'wrong_size').toList();
      for (final file in damaged) {
        final path = File('${modelsDir.path}${Platform.pathSeparator}${file.file}');
        if (path.existsSync()) await path.delete();
      }
      if (damaged.isNotEmpty) notice = const DamagedModels();
      await _unpack(files, statuses);
      _downloading = null;
      _setStatuses(await _check(dir, verifyHash: false));
      if (models[modelId] is ModelReady && !canRun(defaults)) {
        await setDefaults(defaults.copyWith(transcriptionModel: () => modelId));
      }
    } on DownloadStopped {
      _downloading = null;
      await checkModels();
    } on Object catch (error) {
      _downloading = null;
      notice = DownloadFailed('$error');
      await checkModels();
    }
    notifyListeners();
  }

  void stopDownload() => _downloader.stop();

  String? _acceleratorBusy;

  /// Downloads and unpacks [modelId]'s accelerator, then removes the archive.
  Future<void> downloadAccelerator(String modelId) async {
    if (_downloading != null || _acceleratorBusy != null) return;
    final model = catalog.firstWhere((m) => m.id == modelId);
    final files = model.files.where((f) => f.zip).toList();
    _acceleratorBusy = modelId;
    void show(ModelStatus status) {
      accelerators = {...accelerators, modelId: status};
      notifyListeners();
    }

    show(
      ModelDownloading(
        DownloadProgress(ModelDownloader.downloaded(modelsDir, files), files.fold(0, (sum, f) => sum + f.size), 0),
      ),
    );
    try {
      await _downloader.download(modelsDir, files, (progress) => show(ModelDownloading(progress)));
      show(const ModelVerifying());
      await _unpack(files, await _check(modelsDir.path, verifyHash: true));
    } on DownloadStopped {
      // What was received stays for a resume.
    } on Object catch (error) {
      notice = DownloadFailed('$error');
    }
    _acceleratorBusy = null;
    await checkModels();
  }

  /// Unpacks the archives among [files] that were checked whole, then removes
  /// every archive among them.
  Future<void> _unpack(List<ModelFile> files, List<FileStatus> statuses) async {
    final dir = modelsDir.path;
    for (final file in files.where((f) => f.zip)) {
      final status = statuses.firstWhere((s) => s.file == file.file).status;
      final archive = '$dir${Platform.pathSeparator}${file.file}';
      if (status == 'extract') {
        await _extract(archive, dir);
      } else if (status != 'ok') {
        notice = const DamagedModels();
      }
      if (File(archive).existsSync()) await File(archive).delete();
    }
  }

  /// Deletes a transcription model's files that no other ready model uses.
  Future<void> deleteModel(String modelId) async {
    final model = catalog.firstWhere((m) => m.id == modelId);
    final shared = {
      for (final other in catalog)
        if (other.id != modelId && models[other.id] is ModelReady) ...other.files.map((f) => f.file),
    };
    for (final file in model.files) {
      if (shared.contains(file.file)) continue;
      // An unpacked accelerator is a folder named after its archive.
      final unpacked = Directory(
        '${modelsDir.path}${Platform.pathSeparator}${file.file.replaceFirst(RegExp(r'\.zip$'), '')}',
      );
      if (file.zip && unpacked.existsSync()) await unpacked.delete(recursive: true);
      for (final path in [
        '${modelsDir.path}${Platform.pathSeparator}${file.file}',
        '${modelsDir.path}${Platform.pathSeparator}${file.file}.part',
      ]) {
        final f = File(path);
        if (f.existsSync()) await f.delete();
      }
    }
    if (defaults.transcriptionModel == modelId) await setDefaults(defaults.copyWith(transcriptionModel: () => null));
    await checkModels();
  }

  /// Checks every model file's hash and removes damaged ones.
  Future<void> verifyModels() async {
    final dir = modelsDir.path;
    final statuses = await _check(dir, verifyHash: true);
    final damaged = statuses.where((s) => s.status == 'wrong_hash' || s.status == 'wrong_size');
    for (final file in damaged) {
      final path = File('${modelsDir.path}${Platform.pathSeparator}${file.file}');
      if (path.existsSync()) await path.delete();
    }
    notice = damaged.isEmpty ? const ModelsVerified() : const DamagedModels();
    await checkModels();
  }

  Future<int> modelsSize() async {
    if (!modelsDir.existsSync()) return 0;
    var total = 0;
    await for (final entity in modelsDir.list()) {
      if (entity is File) total += await entity.length();
    }
    return total;
  }

  // --- Settings ---------------------------------------------------------------

  Future<void> _loadSettings() async {
    try {
      defaults = JobSettings.fromJson(jsonDecode(await _settingsFile.readAsString()) as Map<String, dynamic>);
    } on Object {
      // A GPU when there is one (docs/DESIGN.md D15: the app chooses).
      defaults = JobSettings(gpuIndex: gpus.isEmpty ? null : 0);
    }
    if ((defaults.gpuIndex ?? -1) >= gpus.length) defaults = defaults.copyWith(gpuIndex: () => null);
    // A model the catalog no longer lists (the small ones before 2026-11).
    final model = defaults.transcriptionModel;
    if (model != null && !catalog.any((m) => m.id == model)) {
      defaults = defaults.copyWith(transcriptionModel: () => null);
    }
    // A language the model no longer offers (any of Whisper's before
    // 2026-11-10, docs/DESIGN.md D43).
    final languages =
        transcriptionModels.where((m) => m.id == defaults.transcriptionModel).firstOrNull?.languages ??
        recommendedTranscription.languages;
    if (defaults.language != 'auto' && !languages.contains(defaults.language)) {
      defaults = defaults.copyWith(language: 'auto');
    }
    if (defaults.listedLanguage != null && !languages.contains(defaults.listedLanguage)) {
      defaults = defaults.copyWith(listedLanguage: () => null);
    }
  }

  Future<void> _loadAppLanguage() async {
    try {
      final json = jsonDecode(await _appFile.readAsString()) as Map<String, dynamic>;
      appLanguage.value = json['language'] as String?;
    } on Object {
      appLanguage.value = null;
    }
  }

  Future<void> setAppLanguage(String? code) async {
    appLanguage.value = code;
    await _support.create(recursive: true);
    await _appFile.writeAsString(jsonEncode({'language': code}));
  }

  Future<void> setDefaults(JobSettings settings) async {
    defaults = settings;
    notifyListeners();
    await _support.create(recursive: true);
    await _settingsFile.writeAsString(jsonEncode(settings.toJson()));
  }

  // --- Library and queue ------------------------------------------------------

  Future<void> _loadLibrary() async {
    library = await store.list();
    notifyListeners();
  }

  LibraryEntry? entry(String id) => library.where((e) => e.id == id).firstOrNull;

  Future<void> enqueue(List<(String path, String name)> files, JobSettings settings, {bool later = false}) =>
      transcriber.enqueue(files, settings, later: later);

  Future<void> rename(String id, String title) async {
    final meta = await store.readMeta(id);
    if (meta == null || title.trim().isEmpty) return;
    await store.writeMeta(id, meta.copyWith(title: title.trim()));
    await _loadLibrary();
  }

  Future<void> delete(String id) async {
    await store.delete(id);
    await _loadLibrary();
  }

  void clearNotice() => notice = null;

  @override
  void dispose() {
    _downloader.stop();
    _finished.close();
    transcriber.dispose();
    appLanguage.dispose();
    super.dispose();
  }
}

// Isolate.run sends its closure, with everything the closure's scope holds, to
// the other isolate. A closure made inside an AppState method can hold the
// state itself (and through its listeners the widget tree), which cannot be
// sent; these functions' closures hold only their arguments.
Future<List<FileStatus>> _check(String dir, {required bool verifyHash}) =>
    Isolate.run(() => Sillok.check(dir, verifyHash: verifyHash));

Future<void> _extract(String archive, String dir) => Isolate.run(() => extractFileToDisk(archive, dir));
