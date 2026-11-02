// The app on the real library and models: first start, a file through the
// queue into the library, and the settings. The run is skipped without
// SILLOK_TEST_MODELS and SILLOK_TEST_AUDIO (see engine_test.dart).
import 'dart:io';

import 'package:flutter/widgets.dart';

import 'package:flutter_test/flutter_test.dart';
import 'package:path_provider_platform_interface/path_provider_platform_interface.dart';
import 'package:plugin_platform_interface/plugin_platform_interface.dart';
import 'package:sillok/app_state.dart';
import 'package:sillok/data/settings.dart';
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
    expect(find.text('Download'), findsOneWidget);
  });

  testWidgets('a file goes through the queue into the library', skip: skip, (tester) async {
    final root = await tester.runAsync(() => Directory.systemTemp.createTemp('sillok app '));
    addTearDown(() => root!.delete(recursive: true));
    PathProviderPlatform.instance = _Paths(root!);
    // The models, linked into the app's folder.
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
    expect(find.text('Open an audio or video file to transcribe it.'), findsOneWidget);

    await tester.runAsync(
      () => state.enqueue([
        (audio!, 'fleurs.wav'),
      ], const JobSettings(language: 'en', diarize: false, transcriptionModel: 'whisper-base-q5_1')),
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
    expect(find.text('Used for new files'), findsOneWidget);
    await tester.dragUntilVisible(find.text('base / q5_1'), find.byType(ListView).last, const Offset(0, -300));
    expect(find.text('base / q5_1'), findsOneWidget);
  });
}
