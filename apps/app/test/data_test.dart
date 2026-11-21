import 'dart:convert';
import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:sillok/data/transcriber.dart';
import 'package:sillok/engine/job_run.dart';
import 'package:sillok/engine/native.dart';
import 'package:sillok/data/keep_awake.dart';
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

class _CountingKeepAwake implements KeepAwake {
  var holds = 0;

  @override
  void hold() => holds++;

  @override
  void release() {}
}

void main() {
  test('timed segments of a piece replace its draft, in its place', () {
    String preview(List<(int, String, bool)> items) => jsonEncode([
      for (final (chunk, text, draft) in items)
        {
          'start_ms': chunk * 30000,
          'end_ms': chunk * 30000 + 1000,
          'text': text,
          'language': 'ko',
          'words': <Object>[],
          'chunk': chunk,
          if (draft) 'draft': true,
        },
    ]);
    var shown = <PreviewSegment>[];
    shown = addPreview(shown, preview([(0, 'heard 0', true)]));
    shown = addPreview(shown, preview([(1, 'heard 1', true)]));
    shown = addPreview(shown, preview([(0, 'a.', false), (0, 'b.', false)]));
    expect([for (final p in shown) p.segment.text], ['a.', 'b.', 'heard 1']);
    expect([for (final p in shown) p.draft], [false, false, true]);
    shown = addPreview(shown, preview([(1, 'c.', false)]));
    expect([for (final p in shown) p.segment.text], ['a.', 'b.', 'c.']);
    // Whisper's pieces come timed, a few segments at a time.
    shown = addPreview(shown, preview([(2, 'd.', false)]));
    shown = addPreview(shown, preview([(2, 'e.', false)]));
    expect([for (final p in shown) p.segment.text], ['a.', 'b.', 'c.', 'd.', 'e.']);
  });

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

  test('a jump lands on the sentence spoken then, or the next one in a pause', () {
    const segments = [
      Segment(startMs: 1000, endMs: 3000, text: 'a', language: 'ko'),
      Segment(startMs: 5000, endMs: 7000, text: 'b', language: 'ko'),
    ];
    int at(int ms, {bool complete = true}) => landingSegment(segments, ms, complete: complete);
    expect(at(0), 0); // before the first
    expect(at(2000), 0);
    expect(at(3000), 1); // a pause: the next one
    expect(at(6999), 1);
    expect(at(9000), 1); // past the end of a finished transcript
    expect(at(9000, complete: false), -1); // not transcribed yet
    expect(landingSegment(const [], 0, complete: true), -1);
  });

  test('sentences at one time, and one without length with the next, play together', () {
    const segments = [
      Segment(startMs: 1000, endMs: 3000, text: 'a', language: 'ko'),
      Segment(startMs: 3000, endMs: 3000, text: 'b', language: 'ko'), // no length: with c
      Segment(startMs: 3000, endMs: 3000, text: 'c', language: 'ko'), // with d, same start
      Segment(startMs: 3000, endMs: 4000, text: 'd', language: 'ko'),
      Segment(startMs: 4500, endMs: 4500, text: 'e', language: 'ko'), // no length: with f
      Segment(startMs: 4600, endMs: 6000, text: 'f', language: 'ko'),
      Segment(startMs: 6000, endMs: 6000, text: 'g', language: 'ko'), // last, alone
    ];
    final groups = playingGroups(segments);
    expect(groups, [0, 1, 1, 1, 4, 4, 6]);
    int at(int ms) => playingGroup(segments, groups, ms);
    expect(at(500), -1);
    expect(at(2999), 0);
    expect(at(3000), 1);
    expect(at(3999), 1);
    expect(at(4200), -1); // a pause
    expect(at(4500), 4);
    expect(at(5999), 4);
    expect(at(6000), -1);
    expect(playingGroups(const []), isEmpty);
  });

  test('a result is titled after its file, without the extension', () {
    expect(fileTitle('강의 1주차.m4a'), '강의 1주차');
    expect(fileTitle('notes.v2.wav'), 'notes.v2');
    expect(fileTitle('.hidden'), '.hidden');
    expect(fileTitle('README'), 'README');
  });

  test('steps are numbered among those the run has: timing words only with an aligner', () {
    RunState run(RunStep step, {required bool aligns, required bool diarize}) => RunState(
      key: 'k',
      name: 'n',
      settings: JobSettings(language: 'ko', diarize: diarize),
      step: step,
      stepFraction: 0,
      remaining: null,
      timingsMs: const {},
      aligns: aligns,
    );
    final whisper = run(RunStep.diarize, aligns: false, diarize: true);
    expect((whisper.stepNumber, whisper.stepCount), (3, 3));
    final qwen = run(RunStep.align, aligns: true, diarize: true);
    expect((qwen.stepNumber, qwen.stepCount), (3, 4));
    final qwenAlone = run(RunStep.diarize, aligns: true, diarize: true);
    expect((qwenAlone.stepNumber, qwenAlone.stepCount), (4, 4));
    expect(run(RunStep.transcribe, aligns: true, diarize: false).stepCount, 3);
  });

  test('preparing evens out the original, transcribing takes the copy as it is', () {
    const settings = JobSettings(language: 'ko', diarize: true, glossary: ['Raft']);
    expect(jobOptions(settings, Lane.prepare), isEmpty);
    final transcribe = jobOptions(settings, Lane.transcribe);
    expect(transcribe, containsPair('even_loudness', false));
    expect(transcribe, containsPair('language', 'ko'));
    expect(transcribe, containsPair('diarize', true));
  });

  test('the queue keeps a file for later with its folder', () async {
    final root = await Directory.systemTemp.createTemp('sillok queue ');
    addTearDown(() => root.delete(recursive: true));
    final store = JobStore(Directory('${root.path}/jobs'));
    final stopped = await store.create(DateTime(2026, 11, 9, 10));
    final leftover = await store.create(DateTime(2026, 11, 9, 11));
    final queueFile = File('${root.path}/queue.json');
    // The first file waits to be started; the second one failed.
    await queueFile.writeAsString(
      jsonEncode([
        QueueItem(
          key: 'a',
          path: '/x/a.m4a',
          name: 'a.m4a',
          settings: const JobSettings(),
          jobId: stopped,
          ready: true,
          later: true,
        ),
        QueueItem(
          key: 'b',
          path: '/x/b.m4a',
          name: 'b.m4a',
          settings: const JobSettings(),
          error: const RunError(Status.mediaError, 'cannot read'),
        ),
      ]),
    );
    final awake = _CountingKeepAwake();
    final transcriber = Transcriber(
      store: store,
      queueFile: queueFile,
      request: (item, lane, copyPath, checkpointPath) => throw StateError('nothing should run'),
      keepAwake: awake,
    );
    addTearDown(transcriber.dispose);
    await transcriber.load();

    expect([for (final item in transcriber.queue) item.later], [true, false]);
    expect(transcriber.run, isNull);
    expect(store.dirOf(stopped).existsSync(), isTrue);
    expect(store.dirOf(leftover).existsSync(), isFalse);
    expect(transcriber.waitingPosition('a'), 0);
    expect(awake.holds, 0); // nothing to run, so the computer may sleep

    // Removed, a file for later takes its folder along.
    await transcriber.remove('a');
    expect(store.dirOf(stopped).existsSync(), isFalse);
    expect((jsonDecode(await queueFile.readAsString()) as List).single['key'], 'b');
  });

  test('Windows perceives the common recording formats as audio or video', () {
    expect(windowsMediaExtensions(), containsAll(['mp3', 'wav', 'm4a', 'mp4']));
    expect(windowsMediaExtensions(), isNot(contains('txt')));
  }, skip: !Platform.isWindows);
}
