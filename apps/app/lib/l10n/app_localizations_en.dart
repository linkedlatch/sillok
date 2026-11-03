// ignore: unused_import
import 'package:intl/intl.dart' as intl;

import 'app_localizations.dart';

// ignore_for_file: type=lint

/// The translations for English (`en`).
class AppLocalizationsEn extends AppLocalizations {
  AppLocalizationsEn([String locale = 'en']) : super(locale);

  @override
  String get appName => 'Sillok';

  @override
  String get ok => 'OK';

  @override
  String get cancel => 'Cancel';

  @override
  String get save => 'Save';

  @override
  String get close => 'Close';

  @override
  String get delete => 'Delete';

  @override
  String get rename => 'Rename';

  @override
  String get retry => 'Retry';

  @override
  String get remove => 'Remove from the list';

  @override
  String get settings => 'Settings';

  @override
  String get add => 'Add files';

  @override
  String get hideList => 'Hide the file list';

  @override
  String get showList => 'Show the file list';

  @override
  String get mediaFiles => 'Audio and video';

  @override
  String get dropFiles => 'Drop to add';

  @override
  String get dropNoMedia => 'No audio or video among the dropped files';

  @override
  String get title => 'Title';

  @override
  String get welcomeTitle => 'Transcribe on this device';

  @override
  String welcomeBody(String size) {
    return 'Sillok turns recordings into text with speakers, entirely on this device: your recordings never leave it. It first needs its speech models, about $size.';
  }

  @override
  String get modelsDownload => 'Download';

  @override
  String get modelsResume => 'Resume';

  @override
  String get modelsStop => 'Pause';

  @override
  String modelsProgress(String done, String total, String speed) {
    return '$done of $total · $speed/s';
  }

  @override
  String get modelsVerifying => 'Checking the files…';

  @override
  String get modelsDamaged => 'Damaged model files were removed. Download them again.';

  @override
  String modelsDownloadFailed(String message) {
    return 'The download stopped: $message';
  }

  @override
  String get modelsVerified => 'All model files are intact.';

  @override
  String modelsCheckFailed(String message) {
    return 'Could not check the model files: $message';
  }

  @override
  String get homeEmpty => 'Open an audio or video file to transcribe it.';

  @override
  String get detailNone => 'Choose a result.';

  @override
  String libraryWaiting(int position) {
    return 'Waiting ($position)';
  }

  @override
  String libraryRunning(String step, int percent) {
    return '$step · $percent%';
  }

  @override
  String libraryRunningLeft(String step, int percent, String left) {
    return '$step · $percent% · about $left left';
  }

  @override
  String libraryFailed(String message) {
    return 'Failed: $message';
  }

  @override
  String get stepRead => 'Reading the file';

  @override
  String get stepCopy => 'Saving a copy for listening';

  @override
  String get stepAnalyze => 'Finding speech';

  @override
  String get stepTranscribe => 'Transcribing';

  @override
  String get stepDiarize => 'Telling speakers apart';

  @override
  String addTitle(int count) {
    String _temp0 = intl.Intl.pluralLogic(
      count,
      locale: localeName,
      other: 'Transcribe $count files',
      one: 'Transcribe a file',
    );
    return '$_temp0';
  }

  @override
  String get addOptions => 'Options';

  @override
  String get addStart => 'Start';

  @override
  String addModelMissing(String model) {
    return '$model is not on this device. Download it in Settings or choose another model.';
  }

  @override
  String get language => 'Language';

  @override
  String get languageAuto => 'Automatic';

  @override
  String get languageKo => 'Korean';

  @override
  String get languageEn => 'English';

  @override
  String get diarize => 'Tell speakers apart';

  @override
  String get diarizeOff => 'No speakers';

  @override
  String get glossary => 'Terms';

  @override
  String get glossaryHint => 'Names and terms in the recording, one a line or separated by commas';

  @override
  String get device => 'Compute device';

  @override
  String get deviceCpu => 'CPU';

  @override
  String get transcriptionModel => 'Transcription model';

  @override
  String get runningCancel => 'Cancel';

  @override
  String get runningCancelling => 'Cancelling…';

  @override
  String runningOverall(String step, int percent) {
    return '$step · $percent%';
  }

  @override
  String runningLeft(String left) {
    return 'About $left left';
  }

  @override
  String get runningPreviews => 'Sentences so far. When done, they are arranged with speakers.';

  @override
  String get runningDetails => 'Details';

  @override
  String stepTime(String step, String time) {
    return '$step: $time';
  }

  @override
  String detailWaiting(int position) {
    return 'Waiting to start: number $position in line.';
  }

  @override
  String get playingSentence => 'Now playing';

  @override
  String get latest => 'Latest';

  @override
  String get search => 'Search';

  @override
  String get searchHint => 'Search the result';

  @override
  String get searchNone => 'No sentence matches.';

  @override
  String get segmentPlay => 'Play from here';

  @override
  String get segmentCopy => 'Copy';

  @override
  String segmentInfo(String start, String end, String language) {
    return '$start – $end · $language';
  }

  @override
  String get copied => 'Copied';

  @override
  String get copyText => 'Copy the text';

  @override
  String get speakers => 'Speakers';

  @override
  String speakerName(String n) {
    return 'Speaker $n';
  }

  @override
  String get speakerListen => 'Listen';

  @override
  String get play => 'Play';

  @override
  String get pause => 'Pause';

  @override
  String get info => 'Details';

  @override
  String infoLength(String time) {
    return 'Recording: $time';
  }

  @override
  String infoTotal(String time, String ratio) {
    return 'Total: $time ($ratio× the recording)';
  }

  @override
  String infoDevice(String device) {
    return 'Transcribed on: $device';
  }

  @override
  String infoFellBack(String device) {
    return '$device (the GPU could not be used)';
  }

  @override
  String get resultMissing => 'This result cannot be read.';

  @override
  String get resultEmpty => 'No speech was found.';

  @override
  String deleteConfirm(String title) {
    return 'Delete “$title”?';
  }

  @override
  String errorUnsupportedFile(String message) {
    return 'Cannot read the audio of this file ($message).';
  }

  @override
  String errorModel(String message) {
    return 'A model cannot be used: $message';
  }

  @override
  String errorEngine(String message) {
    return 'Could not transcribe: $message';
  }

  @override
  String get settingsDefaults => 'Defaults for new files';

  @override
  String get settingsModels => 'Transcription models';

  @override
  String get settingsModelsHint =>
      'large-v3 is slower than turbo and may be a little more accurate. q5_0 and q8_0 are compressed versions, smaller and faster; f16 is the original. The recommended model suits most devices.';

  @override
  String get modelRecommended => 'Recommended';

  @override
  String get modelInUse => 'Used for new files';

  @override
  String get modelUse => 'Use for new files';

  @override
  String modelSummary(String accuracy, String speed, String quantization, String size) {
    return 'Accuracy $accuracy · speed $speed · $quantization · $size';
  }

  @override
  String get accuracyHighest => 'highest';

  @override
  String get accuracyHigh => 'high';

  @override
  String get speedVerySlow => 'very slow';

  @override
  String get speedSlow => 'slow';

  @override
  String get speedMedium => 'medium';

  @override
  String get quantF16 => 'uncompressed';

  @override
  String get quantQ8 => 'lightly compressed';

  @override
  String get quantQ5 => 'compressed';

  @override
  String get settingsStorage => 'Storage';

  @override
  String storageModels(String size) {
    return 'Models: $size';
  }

  @override
  String storageHistory(String size) {
    return 'Results and recordings: $size';
  }

  @override
  String get settingsMore => 'More';

  @override
  String get verifyModels => 'Check the model files';

  @override
  String get about => 'About';

  @override
  String version(String version) {
    return 'Version: $version';
  }

  @override
  String deviceWithNpu(String device) {
    return '$device + NPU';
  }

  @override
  String get npuReady => 'NPU encoder downloaded';

  @override
  String get npu => 'Run the encoder on the NPU';

  @override
  String get npuHint => 'With the CPU, transcription leaves the GPU alone';

  @override
  String get npuUnpacking => 'Checking and unpacking…';

  @override
  String npuDownload(String size) {
    return 'Download the NPU encoder ($size)';
  }
}
