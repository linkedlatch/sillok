// Names of the languages the transcription models know, and which of them
// the system speaks.
import 'dart:ui';

import 'language_names.g.dart';

/// The name of language [code] in the app's language [localeName], from
/// Unicode CLDR (language_names.g.dart): in English when the app's language
/// has no name for it, and the code itself when neither has.
String languageNameOf(String code, String localeName) {
  final names = languageNames[localeName] ?? languageNames[localeName.split('_').first];
  return names?[code] ?? languageNames['en']?[code] ?? code;
}

/// What language [code] calls itself ("日本語"), or null where CLDR does not
/// say.
String? languageSelfName(String code) => languageSelfNames[code];

/// The language of [locale] among [languages], or null when the models do
/// not know it.
String? systemLanguage(Locale locale, List<String> languages) {
  final code = locale.languageCode.toLowerCase();
  return languages.contains(code) ? code : null;
}
