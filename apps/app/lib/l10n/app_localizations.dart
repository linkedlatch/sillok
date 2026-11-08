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
  AppLocalizations(String locale) : localeName = intl.Intl.canonicalizedLocale(locale.toString());

  final String localeName;

  static AppLocalizations of(BuildContext context) {
    return Localizations.of<AppLocalizations>(context, AppLocalizations)!;
  }

  static const LocalizationsDelegate<AppLocalizations> delegate = _AppLocalizationsDelegate();

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
  static const List<LocalizationsDelegate<dynamic>> localizationsDelegates = <LocalizationsDelegate<dynamic>>[
    delegate,
    GlobalMaterialLocalizations.delegate,
    GlobalCupertinoLocalizations.delegate,
    GlobalWidgetsLocalizations.delegate,
  ];

  /// A list of this localizations delegate's supported locales.
  static const List<Locale> supportedLocales = <Locale>[Locale('en'), Locale('ko')];

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

  /// No description provided for @hideList.
  ///
  /// In en, this message translates to:
  /// **'Hide the file list'**
  String get hideList;

  /// No description provided for @showList.
  ///
  /// In en, this message translates to:
  /// **'Show the file list'**
  String get showList;

  /// No description provided for @mediaFiles.
  ///
  /// In en, this message translates to:
  /// **'Audio and video'**
  String get mediaFiles;

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
  /// **'Sillok turns recordings into text with speakers, entirely on this device: your recordings never leave it. First choose a transcription model to download; the models for telling speakers apart and finding speech come with it.'**
  String get welcomeBody;

  /// No description provided for @modelsDownload.
  ///
  /// In en, this message translates to:
  /// **'Download ({size})'**
  String modelsDownload(String size);

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
  /// **'{step} ({number}/{count}) · {percent}%'**
  String libraryRunning(String step, int number, int count, int percent);

  /// No description provided for @libraryRunningLeft.
  ///
  /// In en, this message translates to:
  /// **'{step} ({number}/{count}) · {percent}% · about {left} left'**
  String libraryRunningLeft(String step, int number, int count, int percent, String left);

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

  /// No description provided for @runningPause.
  ///
  /// In en, this message translates to:
  /// **'Pause'**
  String get runningPause;

  /// No description provided for @runningPausing.
  ///
  /// In en, this message translates to:
  /// **'Pausing…'**
  String get runningPausing;

  /// No description provided for @libraryPaused.
  ///
  /// In en, this message translates to:
  /// **'Paused'**
  String get libraryPaused;

  /// No description provided for @detailPaused.
  ///
  /// In en, this message translates to:
  /// **'Paused. It continues where it stopped.'**
  String get detailPaused;

  /// No description provided for @unpause.
  ///
  /// In en, this message translates to:
  /// **'Continue'**
  String get unpause;

  /// No description provided for @runningStep.
  ///
  /// In en, this message translates to:
  /// **'{step} (step {number} of {count}) · {percent}%'**
  String runningStep(String step, int number, int count, int percent);

  /// No description provided for @runningLeft.
  ///
  /// In en, this message translates to:
  /// **'About {left} left in this step'**
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

  /// No description provided for @playingSentence.
  ///
  /// In en, this message translates to:
  /// **'Now playing'**
  String get playingSentence;

  /// No description provided for @latest.
  ///
  /// In en, this message translates to:
  /// **'Latest'**
  String get latest;

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
  /// **'large-v3 is slower than turbo and may be a little more accurate. q5_0 and q8_0 are compressed versions, smaller and faster; f16 is the original. The recommended model suits most devices.'**
  String get settingsModelsHint;

  /// No description provided for @modelRecommended.
  ///
  /// In en, this message translates to:
  /// **'Recommended'**
  String get modelRecommended;

  /// No description provided for @modelSummary.
  ///
  /// In en, this message translates to:
  /// **'Accuracy {accuracy} · speed {speed} · {quantization} · {size}'**
  String modelSummary(String accuracy, String speed, String quantization, String size);

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
  /// **'Check the model files for damage'**
  String get verifyModels;

  /// No description provided for @verifyModelsHint.
  ///
  /// In en, this message translates to:
  /// **'Reads each downloaded model file through and compares it with the original. Damaged files are removed, to be downloaded again. Large files can take tens of seconds.'**
  String get verifyModelsHint;

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

  /// No description provided for @deviceWithNpu.
  ///
  /// In en, this message translates to:
  /// **'{device} + NPU'**
  String deviceWithNpu(String device);

  /// No description provided for @npuReady.
  ///
  /// In en, this message translates to:
  /// **'NPU encoder downloaded'**
  String get npuReady;

  /// No description provided for @npu.
  ///
  /// In en, this message translates to:
  /// **'Run the encoder on the NPU'**
  String get npu;

  /// No description provided for @npuHint.
  ///
  /// In en, this message translates to:
  /// **'With the CPU, transcription leaves the GPU alone'**
  String get npuHint;

  /// No description provided for @npuUnpacking.
  ///
  /// In en, this message translates to:
  /// **'Checking and unpacking…'**
  String get npuUnpacking;

  /// No description provided for @npuDownload.
  ///
  /// In en, this message translates to:
  /// **'Download the NPU encoder ({size})'**
  String npuDownload(String size);

  /// No description provided for @modelsDownloadWithNpu.
  ///
  /// In en, this message translates to:
  /// **'Download with the NPU encoder ({size})'**
  String modelsDownloadWithNpu(String size);

  /// No description provided for @modelsDownloadOnly.
  ///
  /// In en, this message translates to:
  /// **'Model only ({size})'**
  String modelsDownloadOnly(String size);

  /// No description provided for @npuRecommended.
  ///
  /// In en, this message translates to:
  /// **'The NPU encoder takes load off the CPU and GPU (recommended).'**
  String get npuRecommended;
}

class _AppLocalizationsDelegate extends LocalizationsDelegate<AppLocalizations> {
  const _AppLocalizationsDelegate();

  @override
  Future<AppLocalizations> load(Locale locale) {
    return SynchronousFuture<AppLocalizations>(lookupAppLocalizations(locale));
  }

  @override
  bool isSupported(Locale locale) => <String>['en', 'ko'].contains(locale.languageCode);

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
