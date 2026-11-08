// The library through FFI, as the build hook built it for this machine. The
// runs with the real models need them in SILLOK_TEST_MODELS and a recording
// in SILLOK_TEST_AUDIO, and are skipped without them.
import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:sillok/data/transcript.dart';
import 'package:sillok/engine/job_run.dart';
import 'package:sillok/engine/native.dart';
import 'package:sillok/engine/sillok.dart';

void main() {
  final models = Platform.environment['SILLOK_TEST_MODELS'];
  final audio = Platform.environment['SILLOK_TEST_AUDIO'];

  // A request on the real models for [audio], with its copy in [dir].
  JobRequest request(Directory dir, {required bool prepare, required Map<String, Object?> options}) => JobRequest(
    modelsDir: models!,
    tempDir: dir.path,
    gpuIndex: null,
    npu: false,
    transcriptionModel: null,
    options: {...options, 'checkpoint': '${dir.path}/checkpoint.jsonl', 'even_loudness': false},
    source: audio!,
    copyPath: '${dir.path}/audio.m4a',
    prepare: prepare,
  );

  Future<List<JobEvent>> events(JobRun run) => run.events.toList();

  group('library', () {
    test('catalog, engine models and devices', () {
      expect(Sillok.version, startsWith('sillok '));
      final catalog = Sillok.catalog();
      expect(catalog.where((m) => m.recommended).map((m) => m.role).toSet(), {
        'voice_detection',
        'transcription',
        'diarization',
      });
      final other = catalog.firstWhere((m) => m.isTranscription && !m.recommended);
      expect(Sillok.engineModels(other.id).map((m) => m.id), contains(other.id));
      expect(
        () => Sillok.engineModels('no-such-model'),
        throwsA(isA<SillokException>().having((e) => e.status, 'status', Status.invalidArgument)),
      );
      expect(Sillok.devices().where((d) => d.type == 'cpu'), isNotEmpty);
    });

    test('a run without the models fails with a model error', () async {
      final dir = await Directory.systemTemp.createTemp('sillok run ');
      addTearDown(() => dir.delete(recursive: true));
      final run = JobRun.start(
        JobRequest(
          modelsDir: dir.path,
          tempDir: dir.path,
          gpuIndex: null,
          npu: false,
          transcriptionModel: null,
          options: const {'language': 'auto', 'diarize': false, 'glossary': <String>[]},
          source: '${dir.path}/missing.m4a',
          copyPath: '${dir.path}/audio.m4a',
          prepare: true,
        ),
      );
      final last = await run.events.last;
      expect(last, isA<JobFailed>().having((e) => e.status, 'status', Status.modelError));
    });

    test(
      'prepares a file, then transcribes its copy with the real models',
      skip: models == null || audio == null ? 'no models or audio' : null,
      () async {
        final dir = await Directory.systemTemp.createTemp('sillok run ');
        addTearDown(() => dir.delete(recursive: true));
        const options = {'language': 'en', 'diarize': true, 'glossary': <String>[]};
        final prepared = await events(JobRun.start(request(dir, prepare: true, options: options)));
        expect(
          prepared.last,
          isA<Prepared>(),
          reason: prepared.last is JobFailed ? (prepared.last as JobFailed).message : null,
        );
        expect((prepared.last as Prepared).timings.keys.toSet(), {'read', 'copy'});
        final copy = File('${dir.path}/audio.m4a');
        expect(copy.lengthSync(), greaterThan(1000));
        expect(File('${copy.path}.part').existsSync(), isFalse);

        // Again: the copy is there, so nothing is read.
        final again = await events(JobRun.start(request(dir, prepare: true, options: options)));
        expect((again.last as Prepared).timings, isEmpty);

        final run = JobRun.start(request(dir, prepare: false, options: options));
        final steps = <RunStep>{};
        var previews = 0;
        JobEvent? last;
        await for (final event in run.events) {
          last = event;
          if (event is StepProgress) steps.add(event.step);
          if (event is Previews) previews += parseSegments(event.segmentsJson).length;
        }
        expect(last, isA<JobDone>(), reason: last is JobFailed ? last.message : null);
        final done = last! as JobDone;
        final transcript = Transcript.parse(done.transcriptJson);
        expect(transcript.segments, isNotEmpty);
        expect(previews, greaterThan(0));
        expect(steps, {RunStep.read, RunStep.analyze, RunStep.transcribe, RunStep.diarize});
        expect(File('${dir.path}/checkpoint.jsonl').existsSync(), isFalse);
      },
      timeout: const Timeout(Duration(minutes: 10)),
    );

    test('cancelling stops the run', skip: models == null || audio == null ? 'no models or audio' : null, () async {
      final dir = await Directory.systemTemp.createTemp('sillok run ');
      addTearDown(() => dir.delete(recursive: true));
      const options = {'language': 'auto', 'diarize': true, 'glossary': <String>[]};
      await events(JobRun.start(request(dir, prepare: true, options: options)));
      final run = JobRun.start(request(dir, prepare: false, options: options));
      JobEvent? last;
      await for (final event in run.events) {
        last = event;
        if (event is StepProgress && event.step == RunStep.transcribe) run.cancel();
      }
      expect(last, isA<JobFailed>().having((e) => e.status, 'status', Status.cancelled));
    }, timeout: const Timeout(Duration(minutes: 5)));
  });
}
