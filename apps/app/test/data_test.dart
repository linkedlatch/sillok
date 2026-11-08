import 'dart:convert';
import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:sillok/data/transcriber.dart';
import 'package:sillok/engine/native.dart';
import 'package:sillok/data/media_types.dart';
import 'package:sillok/data/settings.dart';
import 'package:sillok/data/store.dart';
import 'package:sillok/data/transcript.dart';

const _document = '''
{"version": 1, "duration_ms": 12000,
 "speakers": [{"id": 0, "name": null}, {"id": 1, "name": null}],
 "segments": [
   {"start_ms": 0, "end_ms": 2000, "text": " 안녕하세요", "language": "ko", "speaker": 0, "words": []},
   {"start_ms": 2500, "end_ms": 4000, "text": "Hello", "language": "en", "speaker": 1, "words": []},
   {"start_ms": 5000, "end_ms": 9000, "text": "다시", "language": "ko", "speaker": 0, "words": []}],
 "models": {"transcription": {"name": "whisper", "device": "CPU", "fell_back_to_cpu": true}},
 "extra": {"kept": true}}
''';

void main() {
  test('paragraphs end at a new speaker, a pause or a minute', () {
    final transcript = Transcript.parse(_document);
    expect(
      [for (final p in paragraphs(transcript.segments)) p.segments],
      [
        [0],
        [1],
        [2],
      ],
    );
    // One speaker, as in a lecture, or none: the same rule.
    for (final speaker in [0, null]) {
      final lecture = [
        Segment(startMs: 0, endMs: 1000, text: 'a', language: 'en', speaker: speaker),
        Segment(startMs: 3500, endMs: 4000, text: 'b', language: 'en', speaker: speaker),
        Segment(startMs: 7500, endMs: 8000, text: 'c', language: 'en', speaker: speaker),
        Segment(startMs: 9000, endMs: 30000, text: 'd', language: 'en', speaker: speaker),
        Segment(startMs: 31000, endMs: 70000, text: 'e', language: 'en', speaker: speaker),
        Segment(startMs: 70500, endMs: 71000, text: 'f', language: 'en', speaker: speaker),
      ];
      expect(
        [for (final p in paragraphs(lecture)) p.segments],
        [
          [0, 1], // a pause of 2.5 s keeps the paragraph
          [2, 3], // 3.5 s ends it
          [4, 5], // e would make it longer than a minute
        ],
      );
    }
  });

  test('renaming a speaker keeps every other field', () {
    final renamed = Transcript.parse(_document).renamed(1, ' Kim ');
    expect(renamed.speakers, {0: null, 1: 'Kim'});
    expect(renamed.segments.first.text, '안녕하세요');
    expect(renamed.stages['transcription']!.fellBackToCpu, isTrue);
    expect(renamed.encode(), contains('"kept":true'));
    expect(renamed.renamed(1, '  ').speakers[1], isNull);
  });

  test('settings round-trip and glossary parsing', () {
    final settings = JobSettings(
      language: 'ko',
      diarize: false,
      glossary: parseGlossary('Kubernetes, 실록\n gRPC \n'),
      gpuIndex: 0,
      npu: false,
    );
    expect(settings.glossary, ['Kubernetes', '실록', 'gRPC']);
    final again = JobSettings.fromJson(settings.toJson());
    expect(again.toJson(), settings.toJson());
    expect(again.options, {
      'language': 'ko',
      'diarize': false,
      'glossary': ['Kubernetes', '실록', 'gRPC'],
    });
    expect(again.copyWith(gpuIndex: () => null).gpuIndex, isNull);
    // Settings saved before the NPU switch use the encoder when it is here.
    expect(JobSettings.fromJson(const {}).npu, isTrue);
  });

  test('the store keeps finished jobs and drops unfinished folders', () async {
    final root = await Directory.systemTemp.createTemp('sillok store ');
    addTearDown(() => root.delete(recursive: true));
    final store = JobStore(root);
    final time = DateTime(2026, 11, 1, 9, 30, 5);
    final first = await store.create(time);
    final second = await store.create(time);
    expect(first, '20261101-093005');
    expect(second, '20261101-093005-1');
    await store.writeTranscript(first, Transcript.parse(_document));
    await store.writeMeta(
      first,
      const JobMeta(title: 'Lecture', source: 'lecture.m4a', createdMs: 1, durationMs: 12000),
    );
    await store.removeUnfinished();
    expect(store.dirOf(second).existsSync(), isFalse);
    final library = await store.list();
    expect(library.map((e) => e.meta.title), ['Lecture']);
    expect((await store.readTranscript(first))!.segments, hasLength(3));
  });

  test('the queue keeps a stopped file paused with its folder', () async {
    final root = await Directory.systemTemp.createTemp('sillok queue ');
    addTearDown(() => root.delete(recursive: true));
    final store = JobStore(Directory('${root.path}/jobs'));
    final stopped = await store.create(DateTime(2026, 11, 9, 10));
    await store.checkpointOf(stopped).writeAsString('{}\n');
    final leftover = await store.create(DateTime(2026, 11, 9, 11));
    final queueFile = File('${root.path}/queue.json');
    // The app closed while the first file ran; the second one failed.
    await queueFile.writeAsString(
      jsonEncode([
        QueueItem(key: 'a', path: '/x/a.m4a', name: 'a.m4a', settings: const JobSettings(), jobId: stopped),
        QueueItem(
          key: 'b',
          path: '/x/b.m4a',
          name: 'b.m4a',
          settings: const JobSettings(),
          error: const RunError(Status.mediaError, 'cannot read'),
        ),
      ]),
    );
    final transcriber = Transcriber(
      store: store,
      queueFile: queueFile,
      request: (item, copyPath, checkpointPath) => throw StateError('nothing should run'),
    );
    addTearDown(transcriber.dispose);
    await transcriber.resume();

    expect([for (final item in transcriber.queue) item.paused], [true, false]);
    expect(transcriber.run, isNull);
    expect(store.checkpointOf(stopped).existsSync(), isTrue);
    expect(store.dirOf(leftover).existsSync(), isFalse);
    expect(transcriber.waitingPosition('a'), 0);

    // Removed, a paused file takes its folder along.
    await transcriber.remove('a');
    expect(store.dirOf(stopped).existsSync(), isFalse);
    expect((jsonDecode(await queueFile.readAsString()) as List).single['key'], 'b');
  });

  test('Windows perceives the common recording formats as audio or video', () {
    expect(windowsMediaExtensions(), containsAll(['mp3', 'wav', 'm4a', 'mp4']));
    expect(windowsMediaExtensions(), isNot(contains('txt')));
  }, skip: !Platform.isWindows);
}
