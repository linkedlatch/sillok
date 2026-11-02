// One transcription in a background isolate. The library's calls block until
// done and call back on the thread making them, so the isolate makes them
// and forwards what the callbacks report.
import 'dart:async';
import 'dart:convert';
import 'dart:ffi';
import 'dart:isolate';

import 'package:ffi/ffi.dart';

import 'native.dart';

/// What a run needs: the engine's configuration, the job's options, the file
/// to read and where to keep the copy for listening back.
class JobRequest {
  const JobRequest({
    required this.modelsDir,
    required this.tempDir,
    required this.gpuIndex,
    required this.transcriptionModel,
    required this.options,
    required this.source,
    required this.copyPath,
  });

  final String modelsDir;
  final String tempDir;

  /// The GPU among the library's GPUs, or null for the CPU.
  final int? gpuIndex;
  final String? transcriptionModel;
  final Map<String, Object?> options;
  final String source;
  final String copyPath;

  String get engineConfig => jsonEncode({
    'models_dir': modelsDir,
    'temp_dir': tempDir,
    'device': gpuIndex == null ? 'cpu' : 'gpu',
    'gpu_index': gpuIndex ?? 0,
    if (transcriptionModel != null) 'transcription_model': transcriptionModel,
  });
}

/// The steps of a run, in order. The last three are the library's stages.
enum RunStep { read, copy, analyze, transcribe, diarize }

sealed class JobEvent {}

/// Progress of [step] as a fraction; each step first reports 0.
final class StepProgress extends JobEvent {
  StepProgress(this.step, this.fraction);

  final RunStep step;
  final double fraction;
}

/// Segments the transcription stage has finished (segments JSON).
final class Previews extends JobEvent {
  Previews(this.segmentsJson);

  final String segmentsJson;
}

/// The transcript document, and how long each step took in milliseconds.
final class JobDone extends JobEvent {
  JobDone(this.transcriptJson, this.timings);

  final String transcriptJson;
  final Map<String, int> timings;
}

/// The run failed with a sillok status ([Status.cancelled] after [JobRun.cancel]).
final class JobFailed extends JobEvent {
  JobFailed(this.status, this.message);

  final int status;
  final String message;
}

/// A running transcription: its [events] end with [JobDone] or [JobFailed].
class JobRun {
  JobRun._();

  final _events = StreamController<JobEvent>();
  int _job = 0; // the job's address while it may be cancelled
  SendPort? _release;
  bool _cancelled = false;

  Stream<JobEvent> get events => _events.stream;

  static JobRun start(JobRequest request) {
    final run = JobRun._();
    final port = ReceivePort();
    final exit = ReceivePort();
    void finish(JobEvent event) {
      run._job = 0;
      // The isolate destroys the job only after this, so a cancel from here
      // never reaches a destroyed job.
      run._release?.send(null);
      run._events
        ..add(event)
        ..close();
      port.close();
      exit.close();
    }

    port.listen((message) {
      switch (message) {
        case final SendPort release:
          run._release = release;
        case final int job:
          run._job = job;
          if (run._cancelled) NativeApi.instance.jobCancel(Pointer.fromAddress(job));
        case final JobDone done:
          finish(done);
        case final JobFailed failed:
          finish(failed);
        case final JobEvent event:
          run._events.add(event);
      }
    });
    // The isolate waits for finish() before it ends, so an end without a
    // result means it failed in Dart code.
    exit.listen((_) {
      if (!run._events.isClosed) finish(JobFailed(Status.internalError, 'the transcription stopped unexpectedly'));
    });
    Isolate.spawn(_work, (port.sendPort, request), onExit: exit.sendPort, errorsAreFatal: true);
    return run;
  }

  /// Stops the run; its events end with [JobFailed] and [Status.cancelled].
  void cancel() {
    _cancelled = true;
    if (_job != 0) NativeApi.instance.jobCancel(Pointer.fromAddress(_job));
  }
}

Future<void> _work((SendPort, JobRequest) start) async {
  final (out, request) = start;
  final native = NativeApi.instance;
  final clock = Stopwatch()..start();
  final timings = <String, int>{};
  RunStep? current;
  var started = 0;
  void enter(RunStep? step) {
    if (step == current) return;
    if (current != null) timings[current!.name] = clock.elapsedMilliseconds - started;
    current = step;
    started = clock.elapsedMilliseconds;
  }

  void report(RunStep step, double fraction) {
    enter(step);
    out.send(StepProgress(step, fraction));
  }

  var fileStep = RunStep.read;
  final fileProgress = NativeCallable<FileProgressFn>.isolateLocal(
    (Pointer<Void> _, double fraction) => report(fileStep, fraction),
  );
  final stageProgress = NativeCallable<ProgressFn>.isolateLocal(
    (Pointer<Void> _, int stage, double fraction) => report(RunStep.values[RunStep.analyze.index + stage], fraction),
  );
  final segments = NativeCallable<SegmentsFn>.isolateLocal(
    (Pointer<Void> _, Pointer<Utf8> json) => out.send(Previews(json.toDartString())),
  );

  var engine = nullptr.cast<SillokEngine>();
  var job = nullptr.cast<SillokJob>();
  final release = ReceivePort();
  out.send(release.sendPort);
  try {
    report(RunStep.read, 0);
    final engineOut = calloc<Pointer<SillokEngine>>();
    final jobOut = calloc<Pointer<SillokJob>>();
    try {
      native.check(withUtf8(request.engineConfig, (config) => native.engineCreate(config, engineOut)));
      engine = engineOut.value;
      native.check(withUtf8(jsonEncode(request.options), (options) => native.jobCreate(engine, options, jobOut)));
      job = jobOut.value;
    } finally {
      calloc
        ..free(engineOut)
        ..free(jobOut);
    }
    out.send(job.address);

    native.check(native.jobOnSegments(job, segments.nativeFunction, nullptr));
    native.check(
      withUtf8(request.source, (path) => native.jobPushFile(job, path, fileProgress.nativeFunction, nullptr)),
    );
    native.check(native.jobEndAudio(job));
    fileStep = RunStep.copy;
    report(RunStep.copy, 0);
    native.check(
      withUtf8(request.copyPath, (path) => native.jobSaveAudio(job, path, fileProgress.nativeFunction, nullptr)),
    );
    final transcript = native.takeString((json) => native.jobRun(job, stageProgress.nativeFunction, nullptr, json));
    enter(null);
    out.send(JobDone(transcript, timings));
  } on SillokException catch (error) {
    out.send(JobFailed(error.status, error.message));
  } catch (error) {
    out.send(JobFailed(Status.internalError, '$error'));
  } finally {
    fileProgress.close();
    stageProgress.close();
    segments.close();
    await release.first;
    release.close();
    if (job != nullptr) native.jobDestroy(job);
    if (engine != nullptr) native.engineDestroy(engine);
  }
}
