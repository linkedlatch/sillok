import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:sillok/app_state.dart';
import 'package:sillok/l10n/app_localizations.dart';
import 'package:sillok/main.dart';
import 'package:sillok/ui/language_field.dart';
import 'package:sillok/ui/language_names.g.dart';
import 'package:sillok/ui/languages.dart';

const _languages = ['ko', 'en', 'ja', 'zh', 'yue', 'de'];

Widget _app(Widget child, {Locale locale = const Locale('ko')}) => MaterialApp(
  locale: locale,
  localizationsDelegates: AppLocalizations.localizationsDelegates,
  supportedLocales: AppLocalizations.supportedLocales,
  home: Scaffold(body: child),
);

void main() {
  test('languages are named in the app language', () {
    expect(languageNameOf('ja', 'ko'), '일본어');
    expect(languageNameOf('ja', 'en'), 'Japanese');
    expect(languageNameOf('yue', 'ko'), '광둥어');
    expect(languageNameOf('xx', 'ko'), 'xx');
  });

  test('every language of the app names the same languages', () {
    // A new translation needs scripts/app/language_names.py run again.
    final english = languageNames['en']!.keys.toSet();
    for (final locale in AppLocalizations.supportedLocales) {
      expect(languageNames[locale.languageCode]?.keys.toSet(), english, reason: '$locale');
    }
  });

  test('the system language as the models write it', () {
    expect(systemLanguage(const Locale('ko', 'KR'), _languages), 'ko');
    expect(systemLanguage(const Locale.fromSubtags(languageCode: 'zh', scriptCode: 'Hant'), _languages), 'zh');
    expect(systemLanguage(const Locale('fr'), _languages), isNull);
  });

  testWidgets('automatic, the system language, English and the list', (tester) async {
    (String, String?)? chosen;
    await tester.pumpWidget(
      _app(
        LanguageField(
          value: 'auto',
          listed: null,
          languages: _languages,
          systemLocale: const Locale('ko', 'KR'),
          onChanged: (code, listed) => chosen = (code, listed),
        ),
      ),
    );
    for (final label in ['자동', '한국어', '영어', '기타']) {
      expect(find.text(label), findsOneWidget);
    }
    await tester.tap(find.text('한국어'));
    expect(chosen, ('ko', null));

    // Nothing kept yet: the segment opens the list.
    await tester.tap(find.text('기타'));
    await tester.pumpAndSettle();
    await tester.enterText(find.byType(TextField), 'deutsch'); // its own name
    await tester.pumpAndSettle();
    expect(find.text('독일어'), findsOneWidget);
    expect(find.text('Deutsch'), findsOneWidget); // shown under it
    await tester.enterText(find.byType(TextField), '일본');
    await tester.pumpAndSettle();
    expect(find.text('독일어'), findsNothing);
    expect(find.text('日本語'), findsOneWidget);
    await tester.tap(find.text('일본어'));
    await tester.pumpAndSettle();
    expect(chosen, ('ja', 'ja'));
  });

  Widget field(String value, void Function(String, String?) onChanged) => _app(
    LanguageField(
      value: value,
      listed: 'ja',
      languages: _languages,
      systemLocale: const Locale('ko', 'KR'),
      onChanged: onChanged,
    ),
  );

  testWidgets('the language kept from the list is chosen at a tap', (tester) async {
    (String, String?)? chosen;
    await tester.pumpWidget(field('auto', (code, listed) => chosen = (code, listed)));
    expect(find.text('기타'), findsNothing);
    await tester.tap(find.text('일본어'));
    await tester.pumpAndSettle();
    expect(find.byType(AlertDialog), findsNothing);
    expect(chosen, ('ja', 'ja'));
    // A language the others offer leaves the kept one as it is.
    await tester.tap(find.text('영어'));
    expect(chosen, ('en', 'ja'));
  });

  testWidgets('tapping the kept language again or its arrow opens the list', (tester) async {
    (String, String?)? chosen;
    await tester.pumpWidget(field('ja', (code, listed) => chosen = (code, listed)));
    await tester.tap(find.text('일본어'));
    await tester.pumpAndSettle();
    expect(find.byType(AlertDialog), findsOneWidget);
    await tester.tap(find.text('독일어'));
    await tester.pumpAndSettle();
    expect(chosen, ('de', 'de'));

    chosen = null;
    await tester.pumpWidget(field('auto', (code, listed) => chosen = (code, listed)));
    await tester.tap(find.byIcon(Icons.arrow_drop_down));
    await tester.pumpAndSettle();
    expect(find.byType(AlertDialog), findsOneWidget);
    expect(chosen, isNull);
  });

  testWidgets('an English system offers English once', (tester) async {
    await tester.pumpWidget(
      _app(
        LanguageField(
          value: 'auto',
          listed: null,
          languages: _languages,
          systemLocale: const Locale('en', 'US'),
          onChanged: (_, _) {},
        ),
        locale: const Locale('en'),
      ),
    );
    expect(find.text('Automatic'), findsOneWidget);
    expect(find.text('English'), findsOneWidget);
    expect(find.text('Other'), findsOneWidget);
  });

  testWidgets('the app speaks the chosen language, or the system language', (tester) async {
    final state = AppState();
    Locale? locale() => tester.widget<MaterialApp>(find.byType(MaterialApp)).locale;
    await tester.pumpWidget(SillokApp(state: state));
    expect(locale(), isNull);
    state.appLanguage.value = 'ko';
    await tester.pump();
    expect(locale(), const Locale('ko'));
    state.appLanguage.value = 'fr'; // not one of the app's
    await tester.pump();
    expect(locale(), isNull);
  });
}
