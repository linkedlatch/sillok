// The app on the real library and models: first start, a file through the
// queue into the library, a paused file continued, and the settings. The runs
// are skipped without
// SILLOK_TEST_MODELS and SILLOK_TEST_AUDIO (see engine_test.dart).
import 'dart:io';

import 'package:flutter/material.dart';

import 'package:flutter_test/flutter_test.dart';
import 'package:path_provider_platform_interface/path_provider_platform_interface.dart';
import 'package:plugin_platform_interface/plugin_platform_interface.dart';
import 'package:sillok/app_state.dart';
import 'package:sillok/data/settings.dart';
import 'package:sillok/engine/job_run.dart';
import 'package:sillok/main.dart';

class _Paths extends PathProviderPlatform with MockPlatformInterfaceMixin {
  _Paths(this.root);

  final Directory root;

  @override
  Future<String?> getApplicationSupportPath() async => '${root.path}/support';

  @override
  Future<String?> getApplicationCachePath() async => '${root.path}/cache';
}

/// Lets real time pass (isolates, files) while the widgets keep up.
Future<void> _waitFor(WidgetTester tester, bool Function() done, {Duration limit = const Duration(minutes: 5)}) async {
  final clock = Stopwatch()..start();
  while (!done()) {
    if (clock.elapsed > limit) fail('timed out');
    await tester.runAsync(() => Future<void>.delayed(const Duration(milliseconds: 200)));
    await tester.pump();
  }
}

void main() {
  final models = Platform.environment['SILLOK_TEST_MODELS'];
  final audio = Platform.environment['SILLOK_TEST_AUDIO'];
  final skip = models == null || audio == null;

  testWidgets('welcome without models', (tester) async {
    final root = await tester.runAsync(() => Directory.systemTemp.createTemp('sillok app '));
    addTearDown(() => root!.delete(recursive: true));
    PathProviderPlatform.instance = _Paths(root!);
    final state = AppState();
    await tester.runAsync(state.start);
    await tester.pumpWidget(SillokApp(state: state));
    await tester.pump();
    expect(find.text('Transcribe on this device'), findsOneWidget);
    // A card for each transcription model.
    expect(find.byType(FilledButton), findsNWidgets(2));
  });

  // The app in a folder of its own, with the models linked into it.
  Future<AppState> started(WidgetTester tester) async {
    final root = await tester.runAsync(() => Directory.systemTemp.createTemp('sillok app '));
    addTearDown(() => root!.delete(recursive: true));
    PathProviderPlatform.instance = _Paths(root!);
    await tester.runAsync(() async {
      final dir = await Directory('${root.path}/support/models').create(recursive: true);
      await for (final file in Directory(models!).list()) {
        if (file is File) await Link('${dir.path}/${file.uri.pathSegments.last}').create(file.path);
      }
    });
    final state = AppState();
    await tester.runAsync(state.start);
    await tester.pumpWidget(SillokApp(state: state));
    await tester.pump();
    return state;
  }

  testWidgets('a paused file continues in its folder', skip: skip, (tester) async {
    final state = await started(tester);
    await tester.runAsync(
      () => state.enqueue([(audio!, 'fleurs.wav')], const JobSettings(language: 'en', diarize: false)),
    );
    // Paused as transcription starts, after the copy for listening back.
    await _waitFor(tester, () => state.transcriber.run?.step == RunStep.transcribe);
    state.transcriber.pause();
    await _waitFor(tester, () => state.transcriber.run == null);
    final item = state.transcriber.queue.single;
    expect(item.paused, isTrue);
    final copy = state.store.audioOf(item.jobId!);
    expect(copy.existsSync(), isTrue);
    final copied = copy.lastModifiedSync();
    await tester.pump();
    expect(find.text('Paused'), findsOneWidget);

    await tester.runAsync(() => state.transcriber.unpause(item.key));
    await _waitFor(tester, () => state.library.isNotEmpty);
    expect(state.transcriber.queue, isEmpty);
    expect(state.library.single.id, item.jobId);
    expect(copy.lastModifiedSync(), copied); // kept, not written again
    expect(state.store.checkpointOf(item.jobId!).existsSync(), isFalse);
  });

  testWidgets('a file added for later outlives its original', skip: skip, (tester) async {
    final state = await started(tester);
    final original = await tester.runAsync(() async {
      final dir = await Directory.systemTemp.createTemp('sillok original ');
      addTearDown(() => dir.delete(recursive: true));
      return File(audio!).copy('${dir.path}/fleurs.wav');
    });
    await tester.runAsync(
      () => state.enqueue(
        [(original!.path, 'fleurs.wav')],
        const JobSettings(language: 'en', diarize: false),
        later: true,
      ),
    );
    await _waitFor(tester, () => state.transcriber.queue.single.ready);
    expect(state.transcriber.run, isNull);
    await tester.pump();
    expect(find.text('To transcribe later'), findsOneWidget);

    await tester.runAsync(() => original!.delete());
    await tester.runAsync(() => state.transcriber.unpause(state.transcriber.queue.single.key));
    await _waitFor(tester, () => state.library.isNotEmpty);
    final transcript = await tester.runAsync(() => state.store.readTranscript(state.library.single.id));
    expect(transcript!.segments, isNotEmpty);
  });

  testWidgets('a file goes through the queue into the library', skip: skip, (tester) async {
    final state = await started(tester);
    expect(find.text('Open an audio or video file to transcribe it.'), findsOneWidget);

    await tester.runAsync(
      () => state.enqueue([(audio!, 'fleurs.wav')], const JobSettings(language: 'en', diarize: false)),
    );
    await _waitFor(tester, () => state.transcriber.run != null);
    expect(find.text('fleurs.wav'), findsOneWidget);
    await _waitFor(tester, () => state.library.isNotEmpty);
    await tester.pump();
    expect(find.text('fleurs'), findsOneWidget);
    expect(state.transcriber.queue, isEmpty);
    expect(state.store.audioOf(state.library.single.id).existsSync(), isTrue);

    await tester.tap(find.byTooltip('Settings'));
    await tester.pumpAndSettle();
    expect(find.text('Transcription models'), findsOneWidget);
    await tester.dragUntilVisible(find.text('large-v3'), find.byType(ListView).last, const Offset(0, -300));
    expect(find.text('q5_0'), findsNWidgets(2));
  });
}
