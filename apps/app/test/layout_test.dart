// Every screen in windows from small to desktop size, both ways round:
// Flutter reports a layout that does not fit (overflow) as an error, which
// fails these tests. Uses the library (catalog) but no models.
import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:path_provider_platform_interface/path_provider_platform_interface.dart';
import 'package:plugin_platform_interface/plugin_platform_interface.dart';
import 'package:sillok/app_state.dart';
import 'package:sillok/data/settings.dart';
import 'package:sillok/data/transcriber.dart';
import 'package:sillok/data/transcript.dart';
import 'package:sillok/engine/job_run.dart';
import 'package:sillok/l10n/app_localizations.dart';
import 'package:sillok/main.dart';
import 'package:sillok/ui/home.dart';
import 'package:sillok/ui/playback.dart';
import 'package:sillok/ui/running_view.dart';
import 'package:sillok/ui/transcript_view.dart';

class _Paths extends PathProviderPlatform with MockPlatformInterfaceMixin {
  _Paths(this.root);

  final Directory root;

  @override
  Future<String?> getApplicationSupportPath() async => '${root.path}/support';

  @override
  Future<String?> getApplicationCachePath() async => '${root.path}/cache';
}

class _SilentPlayback implements Playback {
  @override
  Stream<Duration> get position => const Stream.empty();
  @override
  Stream<Duration> get duration => const Stream.empty();
  @override
  Stream<bool> get playing => const Stream.empty();
  @override
  Future<void> open(String path) async {}
  @override
  Future<void> seek(Duration position) async {}
  @override
  Future<void> play() async {}
  @override
  Future<void> pause() async {}
  @override
  Future<void> dispose() async {}
}

/// A player whose position and state the test sets.
class _ScriptedPlayback implements Playback {
  final _position = StreamController<Duration>.broadcast();
  final _playing = StreamController<bool>.broadcast();

  void playAt(int ms) {
    _playing.add(true);
    _position.add(Duration(milliseconds: ms));
  }

  @override
  Stream<Duration> get position => _position.stream;
  @override
  Stream<Duration> get duration => const Stream.empty();
  @override
  Stream<bool> get playing => _playing.stream;
  @override
  Future<void> open(String path) async {}
  @override
  Future<void> seek(Duration position) async {}
  @override
  Future<void> play() async {}
  @override
  Future<void> pause() async {}
  @override
  Future<void> dispose() async {}
}

const _sizes = {
  'narrow window': Size(360, 780),
  'short window': Size(780, 360),
  'tall window': Size(820, 1180),
  'wide window': Size(1180, 820),
  'desktop': Size(1440, 900),
  'small window': Size(640, 480),
};

const _sentence = '오늘 강의에서는 분산 시스템의 합의 알고리즘과 장애 허용에 대해 이야기하겠습니다';

/// A finished job with a long transcript, three speakers and no audio.
Future<void> _writeResult(Directory support) async {
  final dir = await Directory('${support.path}/jobs/20261101-090000').create(recursive: true);
  final segments = [
    for (var i = 0; i < 60; i++)
      {
        'start_ms': i * 5000,
        'end_ms': i * 5000 + 4500,
        'text': '$_sentence ($i)',
        'language': 'ko',
        'speaker': i ~/ 4 % 3,
        'words': <Object>[],
      },
  ];
  await File('${dir.path}/transcript.json').writeAsString(
    jsonEncode({
      'version': 1,
      'duration_ms': 300000,
      'speakers': [
        for (var i = 0; i < 3; i++) {'id': i, 'name': null},
      ],
      'segments': segments,
      'models': {
        'transcription': {
          'name': 'whisper-large-v3-turbo-q5_0',
          'device': 'Vulkan0 (NVIDIA GeForce RTX 5060 Ti)',
          'fell_back_to_cpu': false,
        },
      },
    }),
  );
  await File('${dir.path}/meta.json').writeAsString(
    jsonEncode({
      'title': '분산 시스템 3주차 강의 녹음 — 합의 알고리즘과 장애 허용',
      'source': 'lecture.m4a',
      'created_ms': DateTime(2026, 11, 1, 9).millisecondsSinceEpoch,
      'duration_ms': 300000,
      'stats': {
        'timings_ms': {'read': 1200, 'copy': 300, 'analyze': 900, 'transcribe': 40000, 'diarize': 5000},
      },
    }),
  );
}

/// Lets real file reads finish, then lets the widgets settle.
Future<void> _settle(WidgetTester tester) async {
  for (var i = 0; i < 5; i++) {
    await tester.runAsync(() => Future<void>.delayed(const Duration(milliseconds: 50)));
    await tester.pump();
  }
  await tester.pumpAndSettle();
}

void main() {
  late Directory root;
  late AppState state;

  Future<void> start(WidgetTester tester, Size size, {required bool result}) async {
    tester.view
      ..physicalSize = size
      ..devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    root = (await tester.runAsync(() => Directory.systemTemp.createTemp('sillok layout ')))!;
    addTearDown(() => root.delete(recursive: true));
    PathProviderPlatform.instance = _Paths(root);
    if (result) await tester.runAsync(() => _writeResult(Directory('${root.path}/support')));
    state = AppState()..newPlayback = _SilentPlayback.new;
    await tester.runAsync(state.start);
    await tester.pumpWidget(SillokApp(state: state));
    await tester.pumpAndSettle();
  }

  for (final MapEntry(key: name, value: size) in _sizes.entries) {
    group(name, () {
      testWidgets('first start', (tester) async {
        await start(tester, size, result: false);
        expect(find.text('Transcribe on this device'), findsOneWidget);
      });

      testWidgets('library, running file and result', (tester) async {
        await start(tester, size, result: true);
        state.transcriber.showRunning(
          const QueueItem(
            key: 'k',
            path: '/x/meeting.m4a',
            name: '팀 회의 녹음 2026-11-01 오후 세션 전체.m4a',
            settings: JobSettings(),
          ),
          const RunState(
            key: 'k',
            name: '팀 회의 녹음 2026-11-01 오후 세션 전체.m4a',
            settings: JobSettings(glossary: ['Raft', 'Paxos']),
            step: RunStep.transcribe,
            stepFraction: 0.4,
            remaining: Duration(minutes: 42),
            timingsMs: {RunStep.read: 1200, RunStep.copy: 300, RunStep.analyze: 900},
          ),
          [
            for (var i = 0; i < 30; i++)
              Segment(startMs: i * 4000, endMs: i * 4000 + 3500, text: _sentence, language: 'ko'),
          ],
        );
        await tester.pumpAndSettle();

        // The step's own progress, not a share of the whole run.
        expect(find.text('Transcribing (4/5) · 40% · about 42:00 left'), findsOneWidget);

        // The running file.
        await tester.tap(find.text('팀 회의 녹음 2026-11-01 오후 세션 전체.m4a').first);
        await tester.pumpAndSettle();
        expect(find.text('Cancel'), findsOneWidget);
        expect(find.text('Pause'), findsOneWidget);
        // Two panes: the add button stays off the detail's buttons.
        final add = find.byType(FloatingActionButton);
        if (add.evaluate().isNotEmpty) {
          for (final label in ['Cancel', 'Pause']) {
            final button = find.ancestor(of: find.text(label), matching: find.bySubtype<ButtonStyleButton>());
            expect(tester.getRect(add).overlaps(tester.getRect(button)), isFalse);
          }
        }
        if (find.byType(BackButton).evaluate().isNotEmpty) {
          await tester.tap(find.byType(BackButton));
          await tester.pumpAndSettle();
        }

        // The result, with search open.
        await tester.tap(find.text('분산 시스템 3주차 강의 녹음 — 합의 알고리즘과 장애 허용').first);
        await _settle(tester);
        await tester.tap(find.byTooltip('Search'));
        await tester.pumpAndSettle();
        await tester.enterText(find.byType(TextField).last, '합의');
        await tester.pumpAndSettle();
        expect(find.text('Play'), findsNothing); // a tooltip, not a label
      });

      testWidgets('paused file', (tester) async {
        await start(tester, size, result: true);
        state.transcriber.showQueue(const [
          QueueItem(
            key: 'p',
            path: '/x/lecture.m4a',
            name: '분산 시스템 4주차 강의 녹음.m4a',
            settings: JobSettings(),
            jobId: '20261109-100000',
            paused: true,
          ),
        ]);
        await tester.pumpAndSettle();
        expect(find.text('Paused'), findsOneWidget);
        await tester.tap(find.text('분산 시스템 4주차 강의 녹음.m4a').first);
        await tester.pumpAndSettle();
        expect(find.text('Paused. It continues where it stopped.'), findsOneWidget);
        expect(find.text('Continue'), findsOneWidget);
      });

      testWidgets('settings', (tester) async {
        await start(tester, size, result: true);
        // As on Apple with the recommended model's NPU encoder here.
        state.accelerators = {state.recommendedTranscription.id: const ModelReady()};
        await tester.tap(find.byTooltip('Settings'));
        await tester.pumpAndSettle();
        expect(find.text('Defaults for new files'), findsOneWidget);
        expect(find.text('Run the encoder on the NPU'), findsOneWidget);
      });

      testWidgets('adding files', (tester) async {
        await start(tester, size, result: true);
        final context = tester.element(find.byType(HomePage));
        unawaited(
          showModalBottomSheet<void>(
            context: context,
            isScrollControlled: true,
            builder: (context) => AddSheet(
              state: state,
              files: const [('/x/a.m4a', '아주 긴 파일 이름을 가진 강의 녹음 파일.m4a'), ('/x/b.mp4', 'b.mp4')],
            ),
          ),
        );
        await tester.pumpAndSettle();
        await tester.tap(find.text(AppLocalizations.of(context).addOptions));
        await tester.pumpAndSettle();
        expect(find.text('Terms'), findsOneWidget);
      });
    });
  }

  testWidgets('the first start offers each model, with the NPU encoder on Apple', (tester) async {
    await start(tester, _sizes['desktop']!, result: false);
    expect(find.text('large-v3 turbo'), findsOneWidget);
    expect(find.text('large-v3'), findsOneWidget);
    // Apple: the model with the NPU encoder (recommended) or alone.
    if (Platform.isMacOS) {
      expect(find.textContaining('Download with the NPU encoder ('), findsNWidgets(2));
      expect(find.textContaining('Model only ('), findsNWidgets(2));
    } else {
      expect(find.textContaining('Download ('), findsNWidgets(2));
    }
    // Under the recommended version, turbo q5_0.
    expect(find.text('Recommended'), findsOneWidget);
  });

  testWidgets('a model card shows the chosen version', (tester) async {
    await start(tester, _sizes['desktop']!, result: true);
    await tester.tap(find.byTooltip('Settings'));
    await tester.pumpAndSettle();
    await tester.dragUntilVisible(find.text('large-v3'), find.byType(ListView).last, const Offset(0, -300));
    expect(find.text('large-v3 turbo'), findsOneWidget);
    expect(find.text('Accuracy high · speed medium · compressed · 574 MB'), findsOneWidget);
    await tester.tap(find.text('f16').first);
    await tester.pumpAndSettle();
    expect(find.text('Accuracy high · speed slow · uncompressed · 1.6 GB'), findsOneWidget);
  });

  testWidgets('playback leaves the text where the reader put it', (tester) async {
    await start(tester, _sizes['desktop']!, result: true);
    final player = _ScriptedPlayback();
    state.newPlayback = () => player;
    await tester.tap(find.text('분산 시스템 3주차 강의 녹음 — 합의 알고리즘과 장애 허용').first);
    await _settle(tester);
    final text = find.descendant(of: find.byType(TranscriptView), matching: find.byType(Scrollable));
    ScrollPosition position() => tester.state<ScrollableState>(text).position;

    // A sentence in view: nothing to go to.
    player.playAt(0);
    await tester.pumpAndSettle();
    expect(find.text('Now playing'), findsNothing);

    // One further down: the text stays, and a button goes there.
    player.playAt(40 * 5000);
    await tester.pumpAndSettle();
    expect(position().pixels, 0);
    await tester.tap(find.text('Now playing'));
    await tester.pumpAndSettle();
    final there = position().pixels;
    expect(there, greaterThan(0));
    expect(find.text('Now playing'), findsNothing);
    player.playAt(48 * 5000);
    await tester.pumpAndSettle();
    expect(position().pixels, there);
  });

  testWidgets('a wide window folds the list away', (tester) async {
    await start(tester, _sizes['desktop']!, result: true);
    const title = '분산 시스템 3주차 강의 녹음 — 합의 알고리즘과 장애 허용';
    await tester.tap(find.text(title).first);
    await _settle(tester);
    final result = tester.state(find.byType(TranscriptView));

    await tester.tap(find.byTooltip('Hide the file list'));
    await tester.pumpAndSettle();
    expect(find.byType(LibraryList), findsNothing);
    expect(find.byType(FloatingActionButton), findsNothing);
    expect(find.byTooltip('Add files'), findsOneWidget);
    // The open result stays as it was.
    expect(tester.state(find.byType(TranscriptView)), same(result));

    await tester.tap(find.byTooltip('Show the file list'));
    await tester.pumpAndSettle();
    expect(find.byType(LibraryList), findsOneWidget);
    expect(tester.state(find.byType(TranscriptView)), same(result));
  });

  testWidgets('the running file follows new sentences only from the end', (tester) async {
    await start(tester, _sizes['desktop']!, result: true);
    const item = QueueItem(key: 'k', path: '/x/a.m4a', name: 'a.m4a', settings: JobSettings());
    const run = RunState(
      key: 'k',
      name: 'a.m4a',
      settings: JobSettings(),
      step: RunStep.transcribe,
      stepFraction: 0.4,
      remaining: null,
      timingsMs: {},
    );
    Future<void> show(int count) async {
      state.transcriber.showRunning(item, run, [
        for (var i = 0; i < count; i++)
          Segment(startMs: i * 4000, endMs: i * 4000 + 3500, text: '$_sentence ($i)', language: 'ko'),
      ]);
      await tester.pumpAndSettle();
    }

    await show(30);
    await tester.tap(find.text('a.m4a').first);
    await tester.pumpAndSettle();
    final sentences = find.descendant(of: find.byType(RunningView), matching: find.byType(Scrollable));
    ScrollPosition position() => tester.state<ScrollableState>(sentences).position;
    await show(40);
    expect(position().pixels, position().maxScrollExtent);
    expect(find.text('Latest'), findsNothing);

    // Scrolled up, the sentences being read stay put.
    await tester.drag(sentences, const Offset(0, 300));
    await tester.pumpAndSettle();
    final reading = position().pixels;
    await show(50);
    expect(position().pixels, reading);
    expect(find.text('Latest'), findsOneWidget);

    await tester.tap(find.text('Latest'));
    await tester.pumpAndSettle();
    await show(60);
    expect(position().pixels, position().maxScrollExtent);
    expect(find.text('Latest'), findsNothing);
  });
}
