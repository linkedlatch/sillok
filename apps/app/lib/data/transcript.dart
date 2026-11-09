// The library's transcript document. The app changes only speaker names and
// keeps every other field as the library wrote it.
import 'dart:convert';

class Segment {
  const Segment({required this.startMs, required this.endMs, required this.text, required this.language, this.speaker});

  factory Segment.fromJson(Map<String, dynamic> json) => Segment(
    startMs: (json['start_ms'] as num).toInt(),
    endMs: (json['end_ms'] as num).toInt(),
    text: (json['text'] as String).trim(),
    language: json['language'] as String? ?? '',
    speaker: (json['speaker'] as num?)?.toInt(),
  );

  final int startMs;
  final int endMs;
  final String text;
  final String language;
  final int? speaker;
}

/// Segments in a preview from the library (a JSON array).
List<Segment> parseSegments(String json) => [
  for (final segment in jsonDecode(json) as List) Segment.fromJson(segment as Map<String, dynamic>),
];

/// What ran a stage: the model and the device, as the library reports.
class StageInfo {
  const StageInfo({required this.name, required this.device, required this.fellBackToCpu});

  final String name;
  final String device;
  final bool fellBackToCpu;
}

class Transcript {
  Transcript(this._json)
    : segments = [for (final segment in _json['segments'] as List) Segment.fromJson(segment as Map<String, dynamic>)];

  factory Transcript.parse(String text) => Transcript(jsonDecode(text) as Map<String, dynamic>);

  final Map<String, dynamic> _json;
  final List<Segment> segments;

  int get durationMs => (_json['duration_ms'] as num).toInt();

  /// Speaker ids with their names; a null name is unnamed.
  Map<int, String?> get speakers => {
    for (final speaker in _json['speakers'] as List? ?? const [])
      (speaker['id'] as num).toInt(): speaker['name'] as String?,
  };

  /// Stages ("voice_detection", "transcription", "diarization") and what ran them.
  Map<String, StageInfo> get stages => {
    for (final MapEntry(:key, :value) in (_json['models'] as Map<String, dynamic>? ?? const {}).entries)
      key: StageInfo(
        name: value['name'] as String? ?? '',
        device: value['device'] as String? ?? '',
        fellBackToCpu: value['fell_back_to_cpu'] as bool? ?? false,
      ),
  };

  /// A copy with speaker [id] named [name] (null or blank: unnamed).
  Transcript renamed(int id, String? name) {
    final copy = jsonDecode(jsonEncode(_json)) as Map<String, dynamic>;
    for (final speaker in copy['speakers'] as List) {
      if ((speaker['id'] as num).toInt() == id) speaker['name'] = (name?.trim().isEmpty ?? true) ? null : name!.trim();
    }
    return Transcript(copy);
  }

  String encode() => jsonEncode(_json);
}

/// Consecutive segments shown together: sentences of one speaker up to a pause
/// longer than three seconds, a minute at most. Measured on a 71-minute
/// lecture: about a hundred paragraphs, half of them 50 seconds or longer.
class Paragraph {
  const Paragraph(this.speaker, this.segments);

  final int? speaker;

  /// Indexes into the transcript's segments.
  final List<int> segments;
}

/// The sentence a jump to [ms] lands on (DESIGN D36): the one spoken then,
/// or in a pause the one that follows. Past the last one, the last when the
/// transcript is [complete]; otherwise none, as the rest is not transcribed
/// yet. -1 for none.
int landingSegment(List<Segment> segments, int ms, {required bool complete}) {
  for (var i = 0; i < segments.length; i++) {
    if (ms < segments[i].endMs) return i;
  }
  return complete && segments.isNotEmpty ? segments.length - 1 : -1;
}

List<Paragraph> paragraphs(List<Segment> segments) {
  final result = <Paragraph>[];
  var current = <int>[];
  for (var i = 0; i < segments.length; i++) {
    if (current.isNotEmpty) {
      final previous = segments[current.last];
      final segment = segments[i];
      final split =
          segment.speaker != previous.speaker ||
          segment.startMs - previous.endMs > 3000 ||
          segment.endMs - segments[current.first].startMs > 60000;
      if (split) {
        result.add(Paragraph(segments[current.first].speaker, current));
        current = [];
      }
    }
    current.add(i);
  }
  if (current.isNotEmpty) result.add(Paragraph(segments[current.first].speaker, current));
  return result;
}
