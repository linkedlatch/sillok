// The queue of files to transcribe, run one at a time. The queue is saved,
// so files added before the app closed are transcribed when it opens again;
// a failed file stays with its error until it is retried or removed.
import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter/foundation.dart';

import '../engine/job_run.dart';
import '../engine/native.dart';
import 'settings.dart';
import 'store.dart';
import 'transcript.dart';

class QueueItem {
  const QueueItem({required this.key, required this.path, required this.name, required this.settings, this.error});

  factory QueueItem.fromJson(Map<String, dynamic> json) => QueueItem(
        key: json['key'] as String,
        path: json['path'] as String,
        name: json['name'] as String,
        settings: JobSettings.fromJson(json['settings'] as Map<String, dynamic>),
        error: json['error'] == null ? null : RunError.fromJson(json['error'] as Map<String, dynamic>),
      );

  final String key;
  final String path;
  final String name;
  final JobSettings settings;
  final RunError? error;

  QueueItem withError(RunError? error) =>
      QueueItem(key: key, path: path, name: name, settings: settings, error: error);

  Map<String, Object?> toJson() =>
      {'key': key, 'path': path, 'name': name, 'settings': settings.toJson(), 'error': error?.toJson()};
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

/// The running file: its step, progress over all steps and the time left.
class RunState {
  const RunState({
    required this.key,
    required this.name,
    required this.settings,
    required this.step,
    required this.stepFraction,
    required this.overall,
    required this.remaining,
    required this.timingsMs,
    this.cancelling = false,
  });

  final String key;
  final String name;
  final JobSettings settings;
  final RunStep step;
  final double stepFraction;
  final double overall;

  /// Null until the estimate means something (3% done and 20 s in).
  final Duration? remaining;

  /// Milliseconds of the steps done so far.
  final Map<RunStep, int> timingsMs;
  final bool cancelling;
}

class Transcriber extends ChangeNotifier {
  Transcriber({required this.store, required this.queueFile, required this.request});

  final JobStore store;
  final File queueFile;

  /// The library request for a file, given its job folder.
  final JobRequest Function(QueueItem item, String copyPath) request;

  List<QueueItem> _queue = [];
  RunState? _run;
  List<Segment> _previews = [];
  JobRun? _job;
  bool _pumping = false;
  var _keys = 0;
  final _finishedIds = <String, String>{};
  final _finished = StreamController<(String key, String id)>.broadcast();

  List<QueueItem> get queue => List.unmodifiable(_queue);
  RunState? get run => _run;
  List<Segment> get previews => List.unmodifiable(_previews);

  /// A file finished: its queue key and its job id in the store.
  Stream<(String key, String id)> get finished => _finished.stream;

  /// The job id of a file that finished and left the queue.
  String? finishedId(String key) => _finishedIds[key];

  /// Loads the saved queue and continues with it.
  Future<void> resume() async {
    try {
      _queue = [
        for (final item in jsonDecode(await queueFile.readAsString()) as List)
          QueueItem.fromJson(item as Map<String, dynamic>),
      ];
    } on Object {
      _queue = [];
    }
    notifyListeners();
    unawaited(_pump());
  }

  /// Queues [files] (path, name) with [settings].
  Future<void> enqueue(List<(String, String)> files, JobSettings settings) async {
    final now = DateTime.now().microsecondsSinceEpoch;
    for (final (path, name) in files) {
      _queue.add(QueueItem(key: '$now-${_keys++}', path: path, name: name, settings: settings));
    }
    await _save();
    notifyListeners();
    unawaited(_pump());
  }

  Future<void> retry(String key) async {
    _queue = [for (final item in _queue) item.key == key ? item.withError(null) : item];
    await _save();
    notifyListeners();
    unawaited(_pump());
  }

  /// Removes a waiting or failed file; the running one is cancelled instead.
  Future<void> remove(String key) async {
    if (_run?.key == key) return cancel();
    _queue.removeWhere((item) => item.key == key);
    await _save();
    notifyListeners();
  }

  void cancel() {
    final run = _run;
    if (run == null) return;
    _run = RunState(
      key: run.key,
      name: run.name,
      settings: run.settings,
      step: run.step,
      stepFraction: run.stepFraction,
      overall: run.overall,
      remaining: run.remaining,
      timingsMs: run.timingsMs,
      cancelling: true,
    );
    _job?.cancel();
    notifyListeners();
  }

  /// Shows [item] as running with [run] and [previews], for tests of the
  /// screens; nothing runs.
  @visibleForTesting
  void showRunning(QueueItem item, RunState run, List<Segment> previews) {
    _queue = [item, ..._queue.where((i) => i.key != item.key)];
    _run = run;
    _previews = previews;
    notifyListeners();
  }

  /// Position of a waiting file among the waiting ones, from 1.
  int waitingPosition(String key) {
    var position = 0;
    for (final item in _queue) {
      if (item.error != null || item.key == _run?.key) continue;
      position++;
      if (item.key == key) return position;
    }
    return 0;
  }

  Future<void> _pump() async {
    if (_pumping) return;
    _pumping = true;
    try {
      while (true) {
        final next = _queue.where((item) => item.error == null).firstOrNull;
        if (next == null) break;
        await _runItem(next);
      }
    } finally {
      _pumping = false;
    }
  }

  Future<void> _runItem(QueueItem item) async {
    final id = await store.create(DateTime.now());
    final created = DateTime.now().millisecondsSinceEpoch;
    final clock = Stopwatch()..start();
    final weights = _weights(item.settings);
    final timings = <RunStep, int>{};
    var stepStarted = 0;
    _previews = [];
    _run = RunState(
      key: item.key,
      name: item.name,
      settings: item.settings,
      step: RunStep.read,
      stepFraction: 0,
      overall: 0,
      remaining: null,
      timingsMs: const {},
    );
    notifyListeners();

    final job = _job = JobRun.start(request(item, store.audioOf(id).path));
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
          var overall = 0.0;
          for (final s in RunStep.values) {
            if (s.index < step.index) overall += weights[s] ?? 0;
          }
          overall += (weights[step] ?? 0) * fraction;
          final elapsed = clock.elapsed;
          _run = RunState(
            key: item.key,
            name: item.name,
            settings: item.settings,
            step: step,
            stepFraction: fraction,
            overall: overall,
            remaining: overall >= 0.03 && elapsed.inSeconds >= 20 ? elapsed * ((1 - overall) / overall) : null,
            timingsMs: Map.unmodifiable(timings),
            cancelling: previous.cancelling,
          );
          notifyListeners();
        case Previews(:final segmentsJson):
          _previews = [..._previews, ...parseSegments(segmentsJson)];
          notifyListeners();
        case JobDone() || JobFailed():
          break;
      }
    }
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
            timingsMs: timings,
            device: transcript.stages['transcription']?.device ?? '',
          ),
        );
        _finishedIds[item.key] = id;
        _queue.removeWhere((q) => q.key == item.key);
        _finished.add((item.key, id));
      case JobFailed(:final status, :final message):
        await store.delete(id);
        if (status == Status.cancelled) {
          _queue.removeWhere((q) => q.key == item.key);
        } else {
          _queue = [for (final q in _queue) q.key == item.key ? q.withError(RunError(status, message)) : q];
        }
      default:
        await store.delete(id);
    }
    await _save();
    notifyListeners();
  }

  /// Share of the whole run each step takes, measured on real runs.
  static Map<RunStep, double> _weights(JobSettings settings) {
    final weights = {
      RunStep.read: 0.03,
      RunStep.copy: 0.01,
      RunStep.analyze: 0.02,
      RunStep.transcribe: 0.84,
      if (settings.diarize) RunStep.diarize: 0.10,
    };
    final total = weights.values.fold(0.0, (a, b) => a + b);
    return {for (final MapEntry(:key, :value) in weights.entries) key: value / total};
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
    _finished.close();
    super.dispose();
  }
}
