import '../data/transcriber.dart';
import '../engine/job_run.dart';
import '../engine/native.dart';
import '../engine/sillok.dart';
import '../l10n/app_localizations.dart';

/// "1:02:03" or "2:03".
String formatTime(int ms) {
  final total = (ms < 0 ? 0 : ms) ~/ 1000;
  final h = total ~/ 3600;
  final m = (total ~/ 60) % 60;
  final s = (total % 60).toString().padLeft(2, '0');
  return h > 0 ? '$h:${m.toString().padLeft(2, '0')}:$s' : '$m:$s';
}

/// "1:02:03.4": a segment's exact time, to a tenth of a second.
String formatPreciseTime(int ms) => '${formatTime(ms)}.${(ms % 1000) ~/ 100}';

String formatBytes(int bytes) {
  const units = ['B', 'KB', 'MB', 'GB'];
  var value = bytes.toDouble();
  var unit = 0;
  while (value >= 1000 && unit < units.length - 1) {
    value /= 1000;
    unit++;
  }
  return unit == 0 ? '$bytes B' : '${value.toStringAsFixed(value < 10 ? 1 : 0)} ${units[unit]}';
}

String formatDate(int ms) {
  final t = DateTime.fromMillisecondsSinceEpoch(ms);
  String two(int n) => n.toString().padLeft(2, '0');
  return '${t.year}-${two(t.month)}-${two(t.day)} ${two(t.hour)}:${two(t.minute)}';
}

String stepName(AppLocalizations l, RunStep step) => switch (step) {
  RunStep.read => l.stepRead,
  RunStep.copy => l.stepCopy,
  RunStep.analyze => l.stepAnalyze,
  RunStep.transcribe => l.stepTranscribe,
  RunStep.diarize => l.stepDiarize,
};

String languageName(AppLocalizations l, String code) => switch (code) {
  'auto' => l.languageAuto,
  'ko' => l.languageKo,
  'en' => l.languageEn,
  _ => code,
};

String errorText(AppLocalizations l, RunError error) => switch (error.status) {
  Status.mediaError => l.errorUnsupportedFile(error.message),
  Status.modelError => l.errorModel(error.message),
  _ => l.errorEngine(error.message),
};

/// "large-v3 turbo"
String familyName(String family) => family.replaceFirst('large-v3-turbo', 'large-v3 turbo');

/// "large-v3 turbo / q5_0"
String modelName(CatalogModel model) => '${familyName(model.family)} / ${model.quantization}';

/// "Accuracy high · speed medium · compressed · 574 MB"; f16 is a step
/// slower than the compressed versions.
String modelSummary(AppLocalizations l, CatalogModel model) {
  final speeds = [l.speedVerySlow, l.speedSlow, l.speedMedium];
  final (accuracy, speed) = model.family == 'large-v3' ? (l.accuracyHighest, 1) : (l.accuracyHigh, 2);
  final quantization = switch (model.quantization) {
    'f16' => l.quantF16,
    'q8_0' => l.quantQ8,
    _ => l.quantQ5,
  };
  return l.modelSummary(
    accuracy,
    speeds[model.quantization == 'f16' ? speed - 1 : speed],
    quantization,
    formatBytes(model.size),
  );
}
