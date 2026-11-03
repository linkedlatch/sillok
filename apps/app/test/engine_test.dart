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
        ),
      );
      final last = await run.events.last;
      expect(last, isA<JobFailed>().having((e) => e.status, 'status', Status.modelError));
    });

    test(
      'transcribes a file with the real models',
      skip: models == null || audio == null ? 'no models or audio' : null,
      () async {
        final dir = await Directory.systemTemp.createTemp('sillok run ');
        addTearDown(() => dir.delete(recursive: true));
        final copy = '${dir.path}/audio.m4a';
        final run = JobRun.start(
          JobRequest(
            modelsDir: models!,
            tempDir: dir.path,
            gpuIndex: null,
            npu: false,
            transcriptionModel: null,
            options: const {'language': 'en', 'diarize': true, 'glossary': <String>[]},
            source: audio!,
            copyPath: copy,
          ),
        );
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
        expect(steps, RunStep.values.toSet());
        expect(done.timings.keys.toSet(), {for (final s in RunStep.values) s.name});
        expect(File(copy).lengthSync(), greaterThan(1000));
      },
      timeout: const Timeout(Duration(minutes: 10)),
    );

    test('cancelling stops the run', skip: models == null || audio == null ? 'no models or audio' : null, () async {
      final dir = await Directory.systemTemp.createTemp('sillok run ');
      addTearDown(() => dir.delete(recursive: true));
      final run = JobRun.start(
        JobRequest(
          modelsDir: models!,
          tempDir: dir.path,
          gpuIndex: null,
          npu: false,
          transcriptionModel: null,
          options: const {'language': 'auto', 'diarize': true, 'glossary': <String>[]},
          source: audio!,
          copyPath: '${dir.path}/audio.m4a',
        ),
      );
      JobEvent? last;
      await for (final event in run.events) {
        last = event;
        if (event is StepProgress && event.step == RunStep.transcribe) run.cancel();
      }
      expect(last, isA<JobFailed>().having((e) => e.status, 'status', Status.cancelled));
    }, timeout: const Timeout(Duration(minutes: 5)));
  });
}
