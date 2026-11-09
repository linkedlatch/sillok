import 'package:flutter/material.dart';

import 'app_state.dart';
import 'l10n/app_localizations.dart';
import 'ui/home.dart';
import 'ui/welcome.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  final state = AppState();
  runApp(SillokApp(state: state));
  state.start();
}

class SillokApp extends StatefulWidget {
  const SillokApp({super.key, required this.state});

  final AppState state;

  @override
  State<SillokApp> createState() => _SillokAppState();
}

class _SillokAppState extends State<SillokApp> {
  final _messenger = GlobalKey<ScaffoldMessengerState>();

  AppState get state => widget.state;

  @override
  void initState() {
    super.initState();
    state.addListener(_showNotice);
  }

  @override
  void dispose() {
    state.removeListener(_showNotice);
    super.dispose();
  }

  /// Shows the state's notice once, on whatever page is open.
  void _showNotice() {
    final notice = state.notice;
    final context = _messenger.currentContext;
    if (notice == null || context == null) return;
    state.clearNotice();
    final l = AppLocalizations.of(context);
    final text = switch (notice) {
      DamagedModels() => l.modelsDamaged,
      DownloadFailed(:final message) => l.modelsDownloadFailed(message),
      ModelsVerified() => l.modelsVerified,
    };
    _messenger.currentState?.showSnackBar(SnackBar(content: Text(text)));
  }

  @override
  Widget build(BuildContext context) =>
      ValueListenableBuilder(valueListenable: state.appLanguage, builder: (context, language, _) => _app(language));

  /// The app in [language], or the system's when null or not one of the
  /// app's own.
  Widget _app(String? language) => MaterialApp(
    scaffoldMessengerKey: _messenger,
    locale: AppLocalizations.supportedLocales.where((l) => l.languageCode == language).firstOrNull,
    onGenerateTitle: (context) => AppLocalizations.of(context).appName,
    // The platform's look as Material 3 offers it, without a theme of our own.
    theme: ThemeData(),
    darkTheme: ThemeData(brightness: Brightness.dark),
    localizationsDelegates: AppLocalizations.localizationsDelegates,
    supportedLocales: AppLocalizations.supportedLocales,
    home: ListenableBuilder(
      listenable: state,
      builder: (context, _) {
        if (!state.started || (!state.modelsChecked && state.modelsError == null)) {
          return const Scaffold(body: Center(child: CircularProgressIndicator()));
        }
        if (state.modelsError != null && !state.modelsChecked) return _ModelCheckFailed(state: state);
        return state.needsWelcome ? WelcomePage(state: state) : HomePage(state: state);
      },
    ),
  );
}

class _ModelCheckFailed extends StatelessWidget {
  const _ModelCheckFailed({required this.state});

  final AppState state;

  @override
  Widget build(BuildContext context) {
    final l = AppLocalizations.of(context);
    return Scaffold(
      body: SafeArea(
        child: Center(
          child: Padding(
            padding: const EdgeInsets.all(24),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              spacing: 16,
              children: [
                Text(l.modelsCheckFailed(state.modelsError ?? ''), textAlign: TextAlign.center),
                FilledButton(onPressed: state.checkModels, child: Text(l.retry)),
              ],
            ),
          ),
        ),
      ),
    );
  }
}
