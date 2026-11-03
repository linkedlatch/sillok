// Every screen at phone, tablet and desktop sizes, both ways round: Flutter
// reports a layout that does not fit (overflow) as an error, which fails
// these tests. Uses the library (catalog) but no models.
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

const _sizes = {
  'phone': Size(360, 780),
  'phone landscape': Size(780, 360),
  'tablet': Size(820, 1180),
  'tablet landscape': Size(1180, 820),
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

  Future<void> start(WidgetTester tester, Size size, {required bool result, double navigationBar = 0}) async {
    tester.view
      ..physicalSize = size
      ..devicePixelRatio = 1
      ..padding = FakeViewPadding(bottom: navigationBar)
      ..viewPadding = FakeViewPadding(bottom: navigationBar);
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
            overall: 0.38,
            remaining: Duration(minutes: 42),
            timingsMs: {RunStep.read: 1200, RunStep.copy: 300, RunStep.analyze: 900},
          ),
          [
            for (var i = 0; i < 30; i++)
              Segment(startMs: i * 4000, endMs: i * 4000 + 3500, text: _sentence, language: 'ko'),
          ],
        );
        await tester.pumpAndSettle();

        // The running file.
        await tester.tap(find.text('팀 회의 녹음 2026-11-01 오후 세션 전체.m4a').first);
        await tester.pumpAndSettle();
        expect(find.text('Cancel'), findsOneWidget);
        // Two panes: the add button stays off the detail's cancel.
        final add = find.byType(FloatingActionButton);
        if (add.evaluate().isNotEmpty) {
          final cancel = find.ancestor(of: find.text('Cancel'), matching: find.bySubtype<ButtonStyleButton>());
          expect(tester.getRect(add).overlaps(tester.getRect(cancel)), isFalse);
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

      testWidgets('settings', (tester) async {
        await start(tester, size, result: true);
        // As on Apple with the recommended model's Neural Engine encoder here.
        state.accelerators = {state.recommendedTranscription.id: const ModelReady()};
        await tester.tap(find.byTooltip('Settings'));
        await tester.pumpAndSettle();
        expect(find.text('Defaults for new files'), findsOneWidget);
        expect(find.text('Run the encoder on the Neural Engine'), findsOneWidget);
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

  // Android draws apps under its navigation bar (edge to edge); nothing the
  // user taps may sit there.
  group('phone with a navigation bar', () {
    const size = Size(360, 780);
    const bar = 48.0;
    void clear(WidgetTester tester, Finder finder) =>
        expect(tester.getRect(finder.first).bottom, lessThanOrEqualTo(size.height - bar));

    testWidgets('library, running file and result', (tester) async {
      await start(tester, size, result: true, navigationBar: bar);
      clear(tester, find.byType(FloatingActionButton));
      state.transcriber.showRunning(
        const QueueItem(key: 'k', path: '/x/a.m4a', name: 'a.m4a', settings: JobSettings()),
        const RunState(
          key: 'k',
          name: 'a.m4a',
          settings: JobSettings(),
          step: RunStep.transcribe,
          stepFraction: 0.4,
          overall: 0.38,
          remaining: null,
          timingsMs: {},
        ),
        const [],
      );
      await tester.pumpAndSettle();
      await tester.tap(find.text('a.m4a').first);
      await tester.pumpAndSettle();
      clear(tester, find.text('Cancel'));
      await tester.tap(find.byType(BackButton));
      await tester.pumpAndSettle();
      await tester.tap(find.text('분산 시스템 3주차 강의 녹음 — 합의 알고리즘과 장애 허용').first);
      await _settle(tester);
      clear(tester, find.byTooltip('Play'));
      clear(tester, find.byType(Slider));
    });

    testWidgets('settings', (tester) async {
      await start(tester, size, result: true, navigationBar: bar);
      await tester.tap(find.byTooltip('Settings'));
      await tester.pumpAndSettle();
      await tester.dragUntilVisible(find.text('About'), find.byType(ListView).last, const Offset(0, -300));
      await tester.drag(find.byType(ListView).last, const Offset(0, -2000));
      await tester.pumpAndSettle();
      clear(tester, find.text('About'));
    });
  });
}
