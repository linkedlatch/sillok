import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_localizations/flutter_localizations.dart';
import 'package:intl/intl.dart' as intl;

import 'app_localizations_en.dart';
import 'app_localizations_ko.dart';

// ignore_for_file: type=lint

/// Callers can lookup localized strings with an instance of AppLocalizations
/// returned by `AppLocalizations.of(context)`.
///
/// Applications need to include `AppLocalizations.delegate()` in their app's
/// `localizationDelegates` list, and the locales they support in the app's
/// `supportedLocales` list. For example:
///
/// ```dart
/// import 'l10n/app_localizations.dart';
///
/// return MaterialApp(
///   localizationsDelegates: AppLocalizations.localizationsDelegates,
///   supportedLocales: AppLocalizations.supportedLocales,
///   home: MyApplicationHome(),
/// );
/// ```
///
/// ## Update pubspec.yaml
///
/// Please make sure to update your pubspec.yaml to include the following
/// packages:
///
/// ```yaml
/// dependencies:
///   # Internationalization support.
///   flutter_localizations:
///     sdk: flutter
///   intl: any # Use the pinned version from flutter_localizations
///
///   # Rest of dependencies
/// ```
///
/// ## iOS Applications
///
/// iOS applications define key application metadata, including supported
/// locales, in an Info.plist file that is built into the application bundle.
/// To configure the locales supported by your app, you’ll need to edit this
/// file.
///
/// First, open your project’s ios/Runner.xcworkspace Xcode workspace file.
/// Then, in the Project Navigator, open the Info.plist file under the Runner
/// project’s Runner folder.
///
/// Next, select the Information Property List item, select Add Item from the
/// Editor menu, then select Localizations from the pop-up menu.
///
/// Select and expand the newly-created Localizations item then, for each
/// locale your application supports, add a new item and select the locale
/// you wish to add from the pop-up menu in the Value field. This list should
/// be consistent with the languages listed in the AppLocalizations.supportedLocales
/// property.
abstract class AppLocalizations {
  AppLocalizations(String locale)
    : localeName = intl.Intl.canonicalizedLocale(locale.toString());

  final String localeName;

  static AppLocalizations of(BuildContext context) {
    return Localizations.of<AppLocalizations>(context, AppLocalizations)!;
  }

  static const LocalizationsDelegate<AppLocalizations> delegate =
      _AppLocalizationsDelegate();

  /// A list of this localizations delegate along with the default localizations
  /// delegates.
  ///
  /// Returns a list of localizations delegates containing this delegate along with
  /// GlobalMaterialLocalizations.delegate, GlobalCupertinoLocalizations.delegate,
  /// and GlobalWidgetsLocalizations.delegate.
  ///
  /// Additional delegates can be added by appending to this list in
  /// MaterialApp. This list does not have to be used at all if a custom list
  /// of delegates is preferred or required.
  static const List<LocalizationsDelegate<dynamic>> localizationsDelegates =
      <LocalizationsDelegate<dynamic>>[
        delegate,
        GlobalMaterialLocalizations.delegate,
        GlobalCupertinoLocalizations.delegate,
        GlobalWidgetsLocalizations.delegate,
      ];

  /// A list of this localizations delegate's supported locales.
  static const List<Locale> supportedLocales = <Locale>[
    Locale('en'),
    Locale('ko'),
  ];

  /// No description provided for @appName.
  ///
  /// In en, this message translates to:
  /// **'Sillok'**
  String get appName;

  /// No description provided for @ok.
  ///
  /// In en, this message translates to:
  /// **'OK'**
  String get ok;

  /// No description provided for @cancel.
  ///
  /// In en, this message translates to:
  /// **'Cancel'**
  String get cancel;

  /// No description provided for @save.
  ///
  /// In en, this message translates to:
  /// **'Save'**
  String get save;

  /// No description provided for @close.
  ///
  /// In en, this message translates to:
  /// **'Close'**
  String get close;

  /// No description provided for @delete.
  ///
  /// In en, this message translates to:
  /// **'Delete'**
  String get delete;

  /// No description provided for @rename.
  ///
  /// In en, this message translates to:
  /// **'Rename'**
  String get rename;

  /// No description provided for @retry.
  ///
  /// In en, this message translates to:
  /// **'Retry'**
  String get retry;

  /// No description provided for @remove.
  ///
  /// In en, this message translates to:
  /// **'Remove from the list'**
  String get remove;

  /// No description provided for @settings.
  ///
  /// In en, this message translates to:
  /// **'Settings'**
  String get settings;

  /// No description provided for @add.
  ///
  /// In en, this message translates to:
  /// **'Add files'**
  String get add;

  /// No description provided for @title.
  ///
  /// In en, this message translates to:
  /// **'Title'**
  String get title;

  /// No description provided for @welcomeTitle.
  ///
  /// In en, this message translates to:
  /// **'Transcribe on this device'**
  String get welcomeTitle;

  /// No description provided for @welcomeBody.
  ///
  /// In en, this message translates to:
  /// **'Sillok turns recordings into text with speakers, entirely on this device: your recordings never leave it. It first needs its speech models, about {size}.'**
  String welcomeBody(String size);

  /// No description provided for @modelsDownload.
  ///
  /// In en, this message translates to:
  /// **'Download'**
  String get modelsDownload;

  /// No description provided for @modelsResume.
  ///
  /// In en, this message translates to:
  /// **'Resume'**
  String get modelsResume;

  /// No description provided for @modelsStop.
  ///
  /// In en, this message translates to:
  /// **'Pause'**
  String get modelsStop;

  /// No description provided for @modelsProgress.
  ///
  /// In en, this message translates to:
  /// **'{done} of {total} · {speed}/s'**
  String modelsProgress(String done, String total, String speed);

  /// No description provided for @modelsVerifying.
  ///
  /// In en, this message translates to:
  /// **'Checking the files…'**
  String get modelsVerifying;

  /// No description provided for @modelsDamaged.
  ///
  /// In en, this message translates to:
  /// **'Damaged model files were removed. Download them again.'**
  String get modelsDamaged;

  /// No description provided for @modelsDownloadFailed.
  ///
  /// In en, this message translates to:
  /// **'The download stopped: {message}'**
  String modelsDownloadFailed(String message);

  /// No description provided for @modelsVerified.
  ///
  /// In en, this message translates to:
  /// **'All model files are intact.'**
  String get modelsVerified;

  /// No description provided for @modelsCheckFailed.
  ///
  /// In en, this message translates to:
  /// **'Could not check the model files: {message}'**
  String modelsCheckFailed(String message);

  /// No description provided for @homeEmpty.
  ///
  /// In en, this message translates to:
  /// **'Open an audio or video file to transcribe it.'**
  String get homeEmpty;

  /// No description provided for @detailNone.
  ///
  /// In en, this message translates to:
  /// **'Choose a result.'**
  String get detailNone;

  /// No description provided for @libraryWaiting.
  ///
  /// In en, this message translates to:
  /// **'Waiting ({position})'**
  String libraryWaiting(int position);

  /// No description provided for @libraryRunning.
  ///
  /// In en, this message translates to:
  /// **'{step} · {percent}%'**
  String libraryRunning(String step, int percent);

  /// No description provided for @libraryRunningLeft.
  ///
  /// In en, this message translates to:
  /// **'{step} · {percent}% · about {left} left'**
  String libraryRunningLeft(String step, int percent, String left);

  /// No description provided for @libraryFailed.
  ///
  /// In en, this message translates to:
  /// **'Failed: {message}'**
  String libraryFailed(String message);

  /// No description provided for @stepRead.
  ///
  /// In en, this message translates to:
  /// **'Reading the file'**
  String get stepRead;

  /// No description provided for @stepCopy.
  ///
  /// In en, this message translates to:
  /// **'Saving a copy for listening'**
  String get stepCopy;

  /// No description provided for @stepAnalyze.
  ///
  /// In en, this message translates to:
  /// **'Finding speech'**
  String get stepAnalyze;

  /// No description provided for @stepTranscribe.
  ///
  /// In en, this message translates to:
  /// **'Transcribing'**
  String get stepTranscribe;

  /// No description provided for @stepDiarize.
  ///
  /// In en, this message translates to:
  /// **'Telling speakers apart'**
  String get stepDiarize;

  /// No description provided for @addTitle.
  ///
  /// In en, this message translates to:
  /// **'{count, plural, =1{Transcribe a file} other{Transcribe {count} files}}'**
  String addTitle(int count);

  /// No description provided for @addOptions.
  ///
  /// In en, this message translates to:
  /// **'Options'**
  String get addOptions;

  /// No description provided for @addStart.
  ///
  /// In en, this message translates to:
  /// **'Start'**
  String get addStart;

  /// No description provided for @addModelMissing.
  ///
  /// In en, this message translates to:
  /// **'{model} is not on this device. Download it in Settings or choose another model.'**
  String addModelMissing(String model);

  /// No description provided for @language.
  ///
  /// In en, this message translates to:
  /// **'Language'**
  String get language;

  /// No description provided for @languageAuto.
  ///
  /// In en, this message translates to:
  /// **'Automatic'**
  String get languageAuto;

  /// No description provided for @languageKo.
  ///
  /// In en, this message translates to:
  /// **'Korean'**
  String get languageKo;

  /// No description provided for @languageEn.
  ///
  /// In en, this message translates to:
  /// **'English'**
  String get languageEn;

  /// No description provided for @diarize.
  ///
  /// In en, this message translates to:
  /// **'Tell speakers apart'**
  String get diarize;

  /// No description provided for @diarizeOff.
  ///
  /// In en, this message translates to:
  /// **'No speakers'**
  String get diarizeOff;

  /// No description provided for @glossary.
  ///
  /// In en, this message translates to:
  /// **'Terms'**
  String get glossary;

  /// No description provided for @glossaryHint.
  ///
  /// In en, this message translates to:
  /// **'Names and terms in the recording, one a line or separated by commas'**
  String get glossaryHint;

  /// No description provided for @device.
  ///
  /// In en, this message translates to:
  /// **'Compute device'**
  String get device;

  /// No description provided for @deviceCpu.
  ///
  /// In en, this message translates to:
  /// **'CPU'**
  String get deviceCpu;

  /// No description provided for @transcriptionModel.
  ///
  /// In en, this message translates to:
  /// **'Transcription model'**
  String get transcriptionModel;

  /// No description provided for @runningCancel.
  ///
  /// In en, this message translates to:
  /// **'Cancel'**
  String get runningCancel;

  /// No description provided for @runningCancelling.
  ///
  /// In en, this message translates to:
  /// **'Cancelling…'**
  String get runningCancelling;

  /// No description provided for @runningOverall.
  ///
  /// In en, this message translates to:
  /// **'{step} · {percent}%'**
  String runningOverall(String step, int percent);

  /// No description provided for @runningLeft.
  ///
  /// In en, this message translates to:
  /// **'About {left} left'**
  String runningLeft(String left);

  /// No description provided for @runningPreviews.
  ///
  /// In en, this message translates to:
  /// **'Sentences so far. When done, they are arranged with speakers.'**
  String get runningPreviews;

  /// No description provided for @runningDetails.
  ///
  /// In en, this message translates to:
  /// **'Details'**
  String get runningDetails;

  /// No description provided for @stepTime.
  ///
  /// In en, this message translates to:
  /// **'{step}: {time}'**
  String stepTime(String step, String time);

  /// No description provided for @detailWaiting.
  ///
  /// In en, this message translates to:
  /// **'Waiting to start: number {position} in line.'**
  String detailWaiting(int position);

  /// No description provided for @follow.
  ///
  /// In en, this message translates to:
  /// **'Follow'**
  String get follow;

  /// No description provided for @search.
  ///
  /// In en, this message translates to:
  /// **'Search'**
  String get search;

  /// No description provided for @searchHint.
  ///
  /// In en, this message translates to:
  /// **'Search the result'**
  String get searchHint;

  /// No description provided for @searchNone.
  ///
  /// In en, this message translates to:
  /// **'No sentence matches.'**
  String get searchNone;

  /// No description provided for @segmentPlay.
  ///
  /// In en, this message translates to:
  /// **'Play from here'**
  String get segmentPlay;

  /// No description provided for @segmentCopy.
  ///
  /// In en, this message translates to:
  /// **'Copy'**
  String get segmentCopy;

  /// No description provided for @segmentInfo.
  ///
  /// In en, this message translates to:
  /// **'{start} – {end} · {language}'**
  String segmentInfo(String start, String end, String language);

  /// No description provided for @copied.
  ///
  /// In en, this message translates to:
  /// **'Copied'**
  String get copied;

  /// No description provided for @copyText.
  ///
  /// In en, this message translates to:
  /// **'Copy the text'**
  String get copyText;

  /// No description provided for @speakers.
  ///
  /// In en, this message translates to:
  /// **'Speakers'**
  String get speakers;

  /// No description provided for @speakerName.
  ///
  /// In en, this message translates to:
  /// **'Speaker {n}'**
  String speakerName(String n);

  /// No description provided for @speakerListen.
  ///
  /// In en, this message translates to:
  /// **'Listen'**
  String get speakerListen;

  /// No description provided for @play.
  ///
  /// In en, this message translates to:
  /// **'Play'**
  String get play;

  /// No description provided for @pause.
  ///
  /// In en, this message translates to:
  /// **'Pause'**
  String get pause;

  /// No description provided for @info.
  ///
  /// In en, this message translates to:
  /// **'Details'**
  String get info;

  /// No description provided for @infoLength.
  ///
  /// In en, this message translates to:
  /// **'Recording: {time}'**
  String infoLength(String time);

  /// No description provided for @infoTotal.
  ///
  /// In en, this message translates to:
  /// **'Total: {time} ({ratio}× the recording)'**
  String infoTotal(String time, String ratio);

  /// No description provided for @infoDevice.
  ///
  /// In en, this message translates to:
  /// **'Transcribed on: {device}'**
  String infoDevice(String device);

  /// No description provided for @infoFellBack.
  ///
  /// In en, this message translates to:
  /// **'{device} (the GPU could not be used)'**
  String infoFellBack(String device);

  /// No description provided for @resultMissing.
  ///
  /// In en, this message translates to:
  /// **'This result cannot be read.'**
  String get resultMissing;

  /// No description provided for @resultEmpty.
  ///
  /// In en, this message translates to:
  /// **'No speech was found.'**
  String get resultEmpty;

  /// No description provided for @deleteConfirm.
  ///
  /// In en, this message translates to:
  /// **'Delete “{title}”?'**
  String deleteConfirm(String title);

  /// No description provided for @errorUnsupportedFile.
  ///
  /// In en, this message translates to:
  /// **'Cannot read the audio of this file ({message}).'**
  String errorUnsupportedFile(String message);

  /// No description provided for @errorModel.
  ///
  /// In en, this message translates to:
  /// **'A model cannot be used: {message}'**
  String errorModel(String message);

  /// No description provided for @errorEngine.
  ///
  /// In en, this message translates to:
  /// **'Could not transcribe: {message}'**
  String errorEngine(String message);

  /// No description provided for @settingsDefaults.
  ///
  /// In en, this message translates to:
  /// **'Defaults for new files'**
  String get settingsDefaults;

  /// No description provided for @settingsModels.
  ///
  /// In en, this message translates to:
  /// **'Transcription models'**
  String get settingsModels;

  /// No description provided for @settingsModelsHint.
  ///
  /// In en, this message translates to:
  /// **'Larger models are more accurate but slower and use more memory. The recommended model suits most devices.'**
  String get settingsModelsHint;

  /// No description provided for @modelRecommended.
  ///
  /// In en, this message translates to:
  /// **'Recommended'**
  String get modelRecommended;

  /// No description provided for @modelInUse.
  ///
  /// In en, this message translates to:
  /// **'Used for new files'**
  String get modelInUse;

  /// No description provided for @modelUse.
  ///
  /// In en, this message translates to:
  /// **'Use for new files'**
  String get modelUse;

  /// No description provided for @modelSummary.
  ///
  /// In en, this message translates to:
  /// **'Accuracy {accuracy} · speed {speed} · {quantization} · {size}'**
  String modelSummary(
    String accuracy,
    String speed,
    String quantization,
    String size,
  );

  /// No description provided for @accuracyHighest.
  ///
  /// In en, this message translates to:
  /// **'highest'**
  String get accuracyHighest;

  /// No description provided for @accuracyHigh.
  ///
  /// In en, this message translates to:
  /// **'high'**
  String get accuracyHigh;

  /// No description provided for @accuracyMedium.
  ///
  /// In en, this message translates to:
  /// **'medium'**
  String get accuracyMedium;

  /// No description provided for @accuracyLow.
  ///
  /// In en, this message translates to:
  /// **'low'**
  String get accuracyLow;

  /// No description provided for @accuracyVeryLow.
  ///
  /// In en, this message translates to:
  /// **'very low'**
  String get accuracyVeryLow;

  /// No description provided for @speedVerySlow.
  ///
  /// In en, this message translates to:
  /// **'very slow'**
  String get speedVerySlow;

  /// No description provided for @speedSlow.
  ///
  /// In en, this message translates to:
  /// **'slow'**
  String get speedSlow;

  /// No description provided for @speedMedium.
  ///
  /// In en, this message translates to:
  /// **'medium'**
  String get speedMedium;

  /// No description provided for @speedFast.
  ///
  /// In en, this message translates to:
  /// **'fast'**
  String get speedFast;

  /// No description provided for @speedVeryFast.
  ///
  /// In en, this message translates to:
  /// **'very fast'**
  String get speedVeryFast;

  /// No description provided for @speedFastest.
  ///
  /// In en, this message translates to:
  /// **'fastest'**
  String get speedFastest;

  /// No description provided for @quantF16.
  ///
  /// In en, this message translates to:
  /// **'uncompressed'**
  String get quantF16;

  /// No description provided for @quantQ8.
  ///
  /// In en, this message translates to:
  /// **'lightly compressed'**
  String get quantQ8;

  /// No description provided for @quantQ5.
  ///
  /// In en, this message translates to:
  /// **'compressed'**
  String get quantQ5;

  /// No description provided for @settingsStorage.
  ///
  /// In en, this message translates to:
  /// **'Storage'**
  String get settingsStorage;

  /// No description provided for @storageModels.
  ///
  /// In en, this message translates to:
  /// **'Models: {size}'**
  String storageModels(String size);

  /// No description provided for @storageHistory.
  ///
  /// In en, this message translates to:
  /// **'Results and recordings: {size}'**
  String storageHistory(String size);

  /// No description provided for @settingsMore.
  ///
  /// In en, this message translates to:
  /// **'More'**
  String get settingsMore;

  /// No description provided for @verifyModels.
  ///
  /// In en, this message translates to:
  /// **'Check the model files'**
  String get verifyModels;

  /// No description provided for @about.
  ///
  /// In en, this message translates to:
  /// **'About'**
  String get about;

  /// No description provided for @version.
  ///
  /// In en, this message translates to:
  /// **'Version: {version}'**
  String version(String version);
}

class _AppLocalizationsDelegate
    extends LocalizationsDelegate<AppLocalizations> {
  const _AppLocalizationsDelegate();

  @override
  Future<AppLocalizations> load(Locale locale) {
    return SynchronousFuture<AppLocalizations>(lookupAppLocalizations(locale));
  }

  @override
  bool isSupported(Locale locale) =>
      <String>['en', 'ko'].contains(locale.languageCode);

  @override
  bool shouldReload(_AppLocalizationsDelegate old) => false;
}

AppLocalizations lookupAppLocalizations(Locale locale) {
  // Lookup logic when only language code is specified.
  switch (locale.languageCode) {
    case 'en':
      return AppLocalizationsEn();
    case 'ko':
      return AppLocalizationsKo();
  }

  throw FlutterError(
    'AppLocalizations.delegate failed to load unsupported locale "$locale". This is likely '
    'an issue with the localizations generation tool. Please file an issue '
    'on GitHub with a reproducible sample app and the gen-l10n configuration '
    'that was used.',
  );
}
