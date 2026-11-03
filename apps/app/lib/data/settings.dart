// What a file is transcribed with. The app keeps defaults for new files, and
// each queued file keeps its own copy.

class JobSettings {
  const JobSettings({
    this.language = 'auto',
    this.diarize = true,
    this.glossary = const [],
    this.transcriptionModel,
    this.gpuIndex,
    this.npu = true,
  });

  factory JobSettings.fromJson(Map<String, dynamic> json) => JobSettings(
    language: json['language'] as String? ?? 'auto',
    diarize: json['diarize'] as bool? ?? true,
    glossary: [for (final term in json['glossary'] as List? ?? const []) term as String],
    transcriptionModel: json['transcription_model'] as String?,
    gpuIndex: (json['gpu_index'] as num?)?.toInt(),
    npu: json['npu'] as bool? ?? true,
  );

  /// "auto" (Korean and English), "ko" or "en".
  final String language;
  final bool diarize;
  final List<String> glossary;

  /// A catalog id, or null for the recommended model.
  final String? transcriptionModel;

  /// A GPU among the library's GPUs, or null for the CPU (the app decides,
  /// docs/DESIGN.md D15).
  final int? gpuIndex;

  /// Whether the encoder runs on the NPU when the model has one here (the
  /// Neural Engine on Apple), with either device.
  final bool npu;

  JobSettings copyWith({
    String? language,
    bool? diarize,
    List<String>? glossary,
    String? Function()? transcriptionModel,
    int? Function()? gpuIndex,
    bool? npu,
  }) => JobSettings(
    language: language ?? this.language,
    diarize: diarize ?? this.diarize,
    glossary: glossary ?? this.glossary,
    transcriptionModel: transcriptionModel != null ? transcriptionModel() : this.transcriptionModel,
    gpuIndex: gpuIndex != null ? gpuIndex() : this.gpuIndex,
    npu: npu ?? this.npu,
  );

  /// The job options for the library.
  Map<String, Object?> get options => {'language': language, 'diarize': diarize, 'glossary': glossary};

  Map<String, Object?> toJson() => {
    'language': language,
    'diarize': diarize,
    'glossary': glossary,
    'transcription_model': transcriptionModel,
    'gpu_index': gpuIndex,
    'npu': npu,
  };
}

/// Terms typed one a line or separated by commas.
List<String> parseGlossary(String text) => [
  for (final term in text.split(RegExp(r'[\n,]')))
    if (term.trim().isNotEmpty) term.trim(),
];
