import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
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
  test('paragraphs follow speakers, or pauses without them', () {
    final transcript = Transcript.parse(_document);
    expect(
      [for (final p in paragraphs(transcript.segments)) p.segments],
      [
        [0],
        [1],
        [2],
      ],
    );
    const plain = [
      Segment(startMs: 0, endMs: 1000, text: 'a', language: 'en'),
      Segment(startMs: 1500, endMs: 2000, text: 'b', language: 'en'),
      Segment(startMs: 5000, endMs: 6000, text: 'c', language: 'en'),
    ];
    expect(
      [for (final p in paragraphs(plain)) p.segments],
      [
        [0, 1],
        [2],
      ],
    );
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

  test('Windows perceives the common recording formats as audio or video', () {
    expect(windowsMediaExtensions(), containsAll(['mp3', 'wav', 'm4a', 'mp4']));
    expect(windowsMediaExtensions(), isNot(contains('txt')));
  }, skip: !Platform.isWindows);
}
