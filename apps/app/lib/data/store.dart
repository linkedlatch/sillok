// Finished transcriptions on disk (docs/APP-NOTES.md, storage format): one
// folder per job, <jobs>/<yyyyMMdd-HHmmss[-n]>/, with transcript.json, the
// copy for listening back (audio.m4a) and meta.json, written last. A folder
// without meta.json is unfinished; while its file is paused or failed, it
// holds the library's checkpoint (checkpoint.jsonl) and maybe the copy.
import 'dart:convert';
import 'dart:io';

import 'transcript.dart';

class JobMeta {
  const JobMeta({
    required this.title,
    required this.source,
    required this.createdMs,
    required this.durationMs,
    this.timingsMs = const {},
    this.device = '',
  });

  factory JobMeta.fromJson(Map<String, dynamic> json) {
    final stats = json['stats'] as Map<String, dynamic>? ?? const {};
    return JobMeta(
      title: json['title'] as String,
      source: json['source'] as String? ?? '',
      createdMs: (json['created_ms'] as num).toInt(),
      durationMs: (json['duration_ms'] as num?)?.toInt() ?? 0,
      timingsMs: {
        for (final MapEntry(:key, :value) in (stats['timings_ms'] as Map<String, dynamic>? ?? const {}).entries)
          key: (value as num).toInt(),
      },
      device: stats['device'] as String? ?? '',
    );
  }

  final String title;
  final String source;
  final int createdMs;
  final int durationMs;

  /// Milliseconds per step: read, copy, analyze, transcribe, diarize.
  final Map<String, int> timingsMs;
  final String device;

  JobMeta copyWith({String? title}) => JobMeta(
    title: title ?? this.title,
    source: source,
    createdMs: createdMs,
    durationMs: durationMs,
    timingsMs: timingsMs,
    device: device,
  );

  Map<String, Object?> toJson() => {
    'title': title,
    'source': source,
    'created_ms': createdMs,
    'duration_ms': durationMs,
    'stats': {'timings_ms': timingsMs, 'device': device},
  };
}

class LibraryEntry {
  const LibraryEntry(this.id, this.meta);

  final String id;
  final JobMeta meta;
}

class JobStore {
  JobStore(this.root);

  final Directory root;

  Directory dirOf(String id) => Directory('${root.path}${Platform.pathSeparator}$id');

  File _file(String id, String name) => File('${dirOf(id).path}${Platform.pathSeparator}$name');

  File audioOf(String id) => _file(id, 'audio.m4a');
  File checkpointOf(String id) => _file(id, 'checkpoint.jsonl');

  /// A new, empty folder named after [time].
  Future<String> create(DateTime time) async {
    String two(int n) => n.toString().padLeft(2, '0');
    final base =
        '${time.year}${two(time.month)}${two(time.day)}-${two(time.hour)}${two(time.minute)}${two(time.second)}';
    for (var n = 0; ; n++) {
      final id = n == 0 ? base : '$base-$n';
      final dir = dirOf(id);
      if (!dir.existsSync()) {
        await dir.create(recursive: true);
        return id;
      }
    }
  }

  /// Finished jobs, newest first.
  Future<List<LibraryEntry>> list() async {
    if (!root.existsSync()) return [];
    final entries = <LibraryEntry>[];
    await for (final dir in root.list()) {
      if (dir is! Directory) continue;
      final id = dir.uri.pathSegments.lastWhere((s) => s.isNotEmpty);
      final meta = await readMeta(id);
      if (meta != null) entries.add(LibraryEntry(id, meta));
    }
    entries.sort((a, b) => b.meta.createdMs.compareTo(a.meta.createdMs));
    return entries;
  }

  /// Removes folders without meta.json but those in [keep]; only while no
  /// job is running.
  Future<void> removeUnfinished({Set<String> keep = const {}}) async {
    if (!root.existsSync()) return;
    await for (final dir in root.list()) {
      if (dir is! Directory) continue;
      final id = dir.uri.pathSegments.lastWhere((s) => s.isNotEmpty);
      if (!keep.contains(id) && !File('${dir.path}${Platform.pathSeparator}meta.json').existsSync()) {
        await dir.delete(recursive: true);
      }
    }
  }

  Future<JobMeta?> readMeta(String id) async {
    final file = _file(id, 'meta.json');
    try {
      return JobMeta.fromJson(jsonDecode(await file.readAsString()) as Map<String, dynamic>);
    } on Object {
      return null;
    }
  }

  /// The segments a stopped run left in the checkpoint: its lines after the
  /// first, one group of speech each (the library's format, DESIGN D34). A
  /// last line cut short is left out.
  Future<List<Segment>> readCheckpoint(String id) async {
    final List<String> lines;
    try {
      lines = await checkpointOf(id).readAsLines();
    } on Object {
      return const [];
    }
    final segments = <Segment>[];
    for (final line in lines.skip(1)) {
      try {
        segments.addAll(parseSegments(line));
      } on Object {
        break;
      }
    }
    return segments;
  }

  Future<Transcript?> readTranscript(String id) async {
    try {
      return Transcript.parse(await _file(id, 'transcript.json').readAsString());
    } on Object {
      return null;
    }
  }

  Future<void> writeTranscript(String id, Transcript transcript) =>
      _writeAtomically(_file(id, 'transcript.json'), transcript.encode());

  Future<void> writeMeta(String id, JobMeta meta) => _writeAtomically(_file(id, 'meta.json'), jsonEncode(meta));

  Future<void> delete(String id) async {
    final dir = dirOf(id);
    if (dir.existsSync()) await dir.delete(recursive: true);
  }

  /// Bytes of every job folder.
  Future<int> size() async {
    if (!root.existsSync()) return 0;
    var total = 0;
    await for (final entity in root.list(recursive: true)) {
      if (entity is File) total += await entity.length();
    }
    return total;
  }

  static Future<void> _writeAtomically(File file, String text) async {
    final temp = File('${file.path}.tmp');
    await temp.writeAsString(text, flush: true);
    await temp.rename(file.path);
  }
}
