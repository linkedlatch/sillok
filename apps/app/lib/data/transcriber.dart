// The queue of files to transcribe. The queue is saved, so files added before
// the app closed are transcribed when it opens again; a failed file stays
// with its error until it is retried or removed.
//
// A file goes through two lanes (DESIGN D20-5, D34). Preparing reads the
// original once, as soon as the file is added and while another file may be
// transcribing, and saves the copy for listening back in the file's job
// folder. Transcribing then runs prepared files one at a time from that copy,
// so the original may be moved or deleted by then. The run writes the
// library's checkpoint to the folder: pausing stops it and keeps both, and
// continuing, retrying or opening the app again picks up after the last group
// of speech transcribed. A file the app closed on while it was transcribing
// comes back paused; one added to transcribe later is prepared and waits,
// paused.
import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter/foundation.dart';

import '../engine/job_run.dart';
import '../engine/native.dart';
import 'keep_awake.dart';
import 'settings.dart';
import 'store.dart';
import 'transcript.dart';

class QueueItem {
  const QueueItem({
    required this.key,
    required this.path,
    required this.name,
    required this.settings,
    this.error,
    this.jobId,
    this.paused = false,
    this.ready = false,
    this.started = false,
    this.prepareMs = const {},
  });

  factory QueueItem.fromJson(Map<String, dynamic> json) => QueueItem(
    key: json['key'] as String,
    path: json['path'] as String,
    name: json['name'] as String,
    settings: JobSettings.fromJson(json['settings'] as Map<String, dynamic>),
    error: json['error'] == null ? null : RunError.fromJson(json['error'] as Map<String, dynamic>),
    jobId: json['job'] as String?,
    paused: json['paused'] as bool? ?? false,
    ready: json['ready'] as bool? ?? false,
    started: json['started'] as bool? ?? false,
    prepareMs: {
      for (final MapEntry(:key, :value) in (json['prepare_ms'] as Map<String, dynamic>? ?? const {}).entries)
        key: (value as num).toInt(),
    },
  );

  final String key;
  final String path;
  final String name;
  final JobSettings settings;
  final RunError? error;

  /// The job folder, once preparing started.
  final String? jobId;

  /// Not to be transcribed until continued: stopped by the user or by the app
  /// closing, or added to transcribe later.
  final bool paused;

  /// The copy for listening back is saved; transcription reads it.
  final bool ready;

  /// Transcription started at least once.
  final bool started;

  /// Milliseconds of reading the original and saving the copy.
  final Map<String, int> prepareMs;

  QueueItem withError(RunError? error) => _copy(error: error);
  QueueItem withJob(String? jobId) => _copy(jobId: jobId);
  QueueItem withPaused(bool paused) => _copy(paused: paused);
  QueueItem withStarted() => _copy(started: true);
  QueueItem withReady(Map<String, int> prepareMs) => _copy(ready: true, prepareMs: prepareMs);

  QueueItem _copy({
    Object? error = _same,
    Object? jobId = _same,
    bool? paused,
    bool? ready,
    bool? started,
    Map<String, int>? prepareMs,
  }) => QueueItem(
    key: key,
    path: path,
    name: name,
    settings: settings,
    error: identical(error, _same) ? this.error : error as RunError?,
    jobId: identical(jobId, _same) ? this.jobId : jobId as String?,
    paused: paused ?? this.paused,
    ready: ready ?? this.ready,
    started: started ?? this.started,
    prepareMs: prepareMs ?? this.prepareMs,
  );

  static const _same = Object();

  Map<String, Object?> toJson() => {
    'key': key,
    'path': path,
    'name': name,
    'settings': settings.toJson(),
    'error': error?.toJson(),
    'job': jobId,
    'paused': paused,
    'ready': ready,
    'started': started,
    'prepare_ms': prepareMs,
  };
}

/// Why a file failed: a sillok status ([Status]) and the library's message.
class RunError {
  const RunError(this.status, this.message);

  factory RunError.fromJson(Map<String, dynamic> json) =>
      RunError((json['status'] as num).toInt(), json['message'] as String);

  final int status;
  final String message;

  Map<String, Object?> toJson() => {'status': status, 'message': message};
}

/// The transcribing file: its step, the progress within it and the time it
/// has left. Steps are shown apart: how long each takes relative to the
/// others varies too much between devices to sum them into one figure.
class RunState {
  const RunState({
    required this.key,
    required this.name,
    required this.settings,
    required this.step,
    required this.stepFraction,
    required this.remaining,
    required this.timingsMs,
    this.cancelling = false,
    this.pausing = false,
  });

  final String key;
  final String name;
  final JobSettings settings;
  final RunStep step;
  final double stepFraction;

  /// Of the current step; null until the estimate means something (3% of
  /// the step done and 20 s into it).
  final Duration? remaining;

  /// Reading the copy, before the steps shown with numbers.
  bool get preparing => step.index < RunStep.analyze.index;

  /// The step's place among the shown steps, from 1: finding speech,
  /// transcribing and telling speakers apart.
  int get stepNumber => step.index - RunStep.analyze.index + 1;
  int get stepCount => settings.diarize ? 3 : 2;

  /// Milliseconds of the steps done so far.
  final Map<RunStep, int> timingsMs;
  final bool cancelling;
  final bool pausing;

  /// A cancel or pause was asked for and the run is stopping.
  bool get stopping => cancelling || pausing;

  RunState _stopping({required bool pause}) => RunState(
    key: key,
    name: name,
    settings: settings,
    step: step,
    stepFraction: stepFraction,
    remaining: remaining,
    timingsMs: timingsMs,
    cancelling: !pause,
    pausing: pause,
  );
}

/// Which work a library request is for.
enum Lane { prepare, transcribe }

class Transcriber extends ChangeNotifier {
  Transcriber({
    required this.store,
    required this.queueFile,
    required this.request,
    this.keepAwake = const NoKeepAwake(),
  });

  final JobStore store;
  final File queueFile;

  /// Held while files are transcribed.
  final KeepAwake keepAwake;

  /// The library request for a file in [lane], given the copy for listening
  /// back and the checkpoint in its job folder.
  final JobRequest Function(QueueItem item, Lane lane, String copyPath, String checkpointPath) request;

  List<QueueItem> _queue = [];
  RunState? _run;
  List<Segment> _previews = [];
  JobRun? _job;
  String? _preparing;
  JobRun? _prepareJob;
  bool _pumping = false;
  bool _preparingLoop = false;
  var _keys = 0;
  final _finishedIds = <String, String>{};
  final _finished = StreamController<(String key, String id)>.broadcast();

  List<QueueItem> get queue => List.unmodifiable(_queue);
  RunState? get run => _run;
  List<Segment> get previews => List.unmodifiable(_previews);

  /// The key of the file being prepared, if any.
  String? get preparing => _preparing;

  /// A file finished: its queue key and its job id in the store.
  Stream<(String key, String id)> get finished => _finished.stream;

  /// The job id of a file that finished and left the queue.
  String? finishedId(String key) => _finishedIds[key];

  /// Loads the saved queue and continues with it. A file that was
  /// transcribing when the app closed stays paused; folders no file in the
  /// queue keeps are left over from a stop and removed.
  Future<void> resume() async {
    try {
      _queue = [
        for (final item in jsonDecode(await queueFile.readAsString()) as List)
          QueueItem.fromJson(item as Map<String, dynamic>),
      ];
    } on Object {
      _queue = [];
    }
    _queue = [for (final item in _queue) item.started && item.error == null ? item.withPaused(true) : item];
    await store.removeUnfinished(keep: {for (final item in _queue) ?item.jobId});
    await _save();
    notifyListeners();
    _kick();
  }

  /// Queues [files] (path, name) with [settings]; with [later], they are
  /// prepared and wait, paused, to be started.
  Future<void> enqueue(List<(String, String)> files, JobSettings settings, {bool later = false}) async {
    final now = DateTime.now().microsecondsSinceEpoch;
    for (final (path, name) in files) {
      _queue.add(QueueItem(key: '$now-${_keys++}', path: path, name: name, settings: settings, paused: later));
    }
    await _save();
    notifyListeners();
    _kick();
  }

  /// Runs a failed file again, from where it stopped.
  Future<void> retry(String key) => _update(key, (item) => item.withError(null));

  /// Starts or continues a paused file, from where it stopped.
  Future<void> unpause(String key) => _update(key, (item) => item.withPaused(false));

  Future<void> _update(String key, QueueItem Function(QueueItem item) change) async {
    _queue = [for (final item in _queue) item.key == key ? change(item) : item];
    await _save();
    notifyListeners();
    _kick();
  }

  /// Removes a file that is not transcribing, with its job folder; the
  /// transcribing one is cancelled instead.
  Future<void> remove(String key) async {
    if (_run?.key == key) return cancel();
    final jobId = _queue.where((item) => item.key == key).firstOrNull?.jobId;
    _queue.removeWhere((item) => item.key == key);
    await _save();
    notifyListeners();
    // A file being prepared takes its folder along once that stops.
    if (_preparing == key) {
      _prepareJob?.cancel();
    } else if (jobId != null) {
      await store.delete(jobId);
    }
  }

  /// Stops the transcribing file and removes it.
  void cancel() => _stop(pause: false);

  /// Stops the transcribing file and keeps it, paused, to continue later.
  void pause() => _stop(pause: true);

  void _stop({required bool pause}) {
    final run = _run;
    if (run == null || run.stopping) return;
    _run = run._stopping(pause: pause);
    _job?.cancel();
    notifyListeners();
  }

  /// Shows [items] as the queue, for tests of the screens; nothing runs.
  @visibleForTesting
  void showQueue(List<QueueItem> items, {String? preparing}) {
    _queue = items;
    _preparing = preparing;
    notifyListeners();
  }

  /// Shows [item] as transcribing with [run] and [previews], for tests of the
  /// screens; nothing runs.
  @visibleForTesting
  void showRunning(QueueItem item, RunState run, List<Segment> previews) {
    _queue = [item, ..._queue.where((i) => i.key != item.key)];
    _run = run;
    _previews = previews;
    notifyListeners();
  }

  /// Position of a waiting file among the files to transcribe, from 1.
  int waitingPosition(String key) {
    var position = 0;
    for (final item in _queue) {
      if (item.error != null || item.paused || item.key == _run?.key) continue;
      position++;
      if (item.key == key) return position;
    }
    return 0;
  }

  void _kick() {
    unawaited(_prepareAll());
    unawaited(_pump());
  }

  QueueItem? _current(String key) => _queue.where((q) => q.key == key).firstOrNull;

  void _replace(QueueItem item) => _queue = [for (final q in _queue) q.key == item.key ? item : q];

  // --- Preparing ---------------------------------------------------------

  Future<void> _prepareAll() async {
    if (_preparingLoop) return;
    _preparingLoop = true;
    try {
      while (true) {
        final next = _queue.where((item) => !item.ready && item.error == null).firstOrNull;
        if (next == null) break;
        await _prepare(next);
      }
    } finally {
      _preparingLoop = false;
    }
  }

  Future<void> _prepare(QueueItem item) async {
    var id = item.jobId;
    if (id == null || !store.dirOf(id).existsSync()) {
      // Saved with the file before preparing, so that a folder the app
      // closes on is kept and found again.
      id = await store.create(DateTime.now());
      item = item.withJob(id);
      _replace(item);
      await _save();
    }
    _preparing = item.key;
    notifyListeners();
    final job = _prepareJob = JobRun.start(
      request(item, Lane.prepare, store.audioOf(id).path, store.checkpointOf(id).path),
    );
    JobEvent? last;
    await for (final event in job.events) {
      last = event;
    }
    _prepareJob = null;
    _preparing = null;

    final current = _current(item.key);
    switch (last) {
      case Prepared(:final timings) when current != null:
        _replace(current.withReady(timings));
      case JobFailed(:final status) when status == Status.cancelled && current != null:
        break; // the app is closing; preparing starts again next time
      case JobFailed(:final status, :final message) when current != null:
        _replace(current.withError(RunError(status, message)));
      default:
        // Removed while it was prepared.
        await store.delete(id);
    }
    await _save();
    notifyListeners();
    unawaited(_pump());
  }

  // --- Transcribing --------------------------------------------------------

  Future<void> _pump() async {
    if (_pumping) return;
    _pumping = true;
    var held = false;
    try {
      while (true) {
        final next = _queue.where((item) => item.ready && item.error == null && !item.paused).firstOrNull;
        if (next == null) break;
        if (!held) keepAwake.hold();
        held = true;
        await _runItem(next);
      }
    } finally {
      if (held) keepAwake.release();
      _pumping = false;
    }
  }

  Future<void> _runItem(QueueItem item) async {
    final id = item.jobId!;
    if (!item.started) {
      item = item.withStarted();
      _replace(item);
      await _save();
    }
    final created = DateTime.now().millisecondsSinceEpoch;
    final clock = Stopwatch()..start();
    final timings = <RunStep, int>{};
    var stepStarted = 0;
    _previews = [];
    _run = RunState(
      key: item.key,
      name: item.name,
      settings: item.settings,
      step: RunStep.read,
      stepFraction: 0,
      remaining: null,
      timingsMs: const {},
    );
    notifyListeners();

    final job = _job = JobRun.start(
      request(item, Lane.transcribe, store.audioOf(id).path, store.checkpointOf(id).path),
    );
    JobEvent? last;
    await for (final event in job.events) {
      last = event;
      switch (event) {
        case StepProgress(:final step, :final fraction):
          final previous = _run!;
          if (step != previous.step) {
            timings[previous.step] = clock.elapsedMilliseconds - stepStarted;
            stepStarted = clock.elapsedMilliseconds;
          }
          final elapsed = Duration(milliseconds: clock.elapsedMilliseconds - stepStarted);
          _run = RunState(
            key: item.key,
            name: item.name,
            settings: item.settings,
            step: step,
            stepFraction: fraction,
            remaining: fraction >= 0.03 && elapsed.inSeconds >= 20 ? elapsed * ((1 - fraction) / fraction) : null,
            timingsMs: Map.unmodifiable(timings),
            cancelling: previous.cancelling,
            pausing: previous.pausing,
          );
          notifyListeners();
        case Previews(:final segmentsJson):
          _previews = [..._previews, ...parseSegments(segmentsJson)];
          notifyListeners();
        case JobDone() || JobFailed() || Prepared():
          break;
      }
    }
    final paused = _run?.pausing ?? false;
    _job = null;
    _run = null;
    _previews = [];

    switch (last) {
      case JobDone(:final transcriptJson, :final timings):
        final transcript = Transcript.parse(transcriptJson);
        await store.writeTranscript(id, transcript);
        await store.writeMeta(
          id,
          JobMeta(
            title: _title(item.name),
            source: item.name,
            createdMs: created,
            durationMs: transcript.durationMs,
            // Reading counts the original and the copy.
            timingsMs: {...item.prepareMs, ...timings, 'read': (item.prepareMs['read'] ?? 0) + (timings['read'] ?? 0)},
            device: transcript.stages['transcription']?.device ?? '',
          ),
        );
        _finishedIds[item.key] = id;
        _queue.removeWhere((q) => q.key == item.key);
        _finished.add((item.key, id));
      case JobFailed(:final status) when status == Status.cancelled && paused:
        _queue = [for (final q in _queue) q.key == item.key ? q.withPaused(true) : q];
      case JobFailed(:final status) when status == Status.cancelled:
        _queue.removeWhere((q) => q.key == item.key);
        await store.delete(id);
      case JobFailed(:final status, :final message):
        // The folder stays, so that a retry continues where this run stopped.
        _queue = [for (final q in _queue) q.key == item.key ? q.withError(RunError(status, message)) : q];
      default:
        _queue = [
          for (final q in _queue) q.key == item.key ? q.withError(const RunError(Status.internalError, '')) : q,
        ];
    }
    await _save();
    notifyListeners();
  }

  static String _title(String name) {
    final dot = name.lastIndexOf('.');
    return dot > 0 ? name.substring(0, dot) : name;
  }

  Future<void> _save() async {
    await queueFile.parent.create(recursive: true);
    final temp = File('${queueFile.path}.tmp');
    await temp.writeAsString(jsonEncode(_queue), flush: true);
    await temp.rename(queueFile.path);
  }

  @override
  void dispose() {
    _job?.cancel();
    _prepareJob?.cancel();
    _finished.close();
    super.dispose();
  }
}
