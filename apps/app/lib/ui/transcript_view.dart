import 'dart:async';

import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter/services.dart';

import '../app_state.dart';
import '../data/store.dart';
import '../data/transcript.dart';
import '../engine/job_run.dart';
import '../engine/sillok.dart';
import '../l10n/app_localizations.dart';
import 'format.dart';
import 'home.dart';
import 'playback.dart';

/// A result: paragraphs by speaker over a player. Choosing a sentence plays
/// it from a second before and shows what is otherwise hidden: its exact
/// times and language.
class TranscriptView extends StatefulWidget {
  const TranscriptView({super.key, required this.state, required this.entry});

  final AppState state;
  final LibraryEntry entry;

  @override
  State<TranscriptView> createState() => _TranscriptViewState();
}

class _TranscriptViewState extends State<TranscriptView> {
  late final Playback _player = widget.state.newPlayback();
  final _subscriptions = <StreamSubscription<Object?>>[];
  final _recognizers = <TapGestureRecognizer>[];
  final _paragraphKeys = <GlobalKey>[];
  final _search = TextEditingController();
  Transcript? _transcript;
  List<Paragraph> _paragraphs = const [];
  bool _loaded = false;
  bool _playing = false;
  bool _searching = false;

  /// Whether the playing sentence is kept in view: scrolling by hand stops
  /// that, and choosing a sentence or the button back to the playing one
  /// starts it again.
  bool _follow = true;
  Duration _position = Duration.zero;
  Duration _duration = Duration.zero;
  int? _selected;
  int? _current;

  String get _id => widget.entry.id;

  @override
  void initState() {
    super.initState();
    _subscriptions
      ..add(_player.position.listen(_onPosition))
      ..add(_player.duration.listen((d) => setState(() => _duration = d)))
      ..add(_player.playing.listen((playing) => setState(() => _playing = playing)));
    _load();
  }

  Future<void> _load() async {
    final transcript = await widget.state.store.readTranscript(_id);
    final audio = widget.state.store.audioOf(_id);
    if (audio.existsSync()) await _player.open(audio.path);
    if (!mounted) return;
    setState(() {
      _setTranscript(transcript);
      _loaded = true;
    });
  }

  void _setTranscript(Transcript? transcript) {
    _transcript = transcript;
    _paragraphs = transcript == null ? const [] : paragraphs(transcript.segments);
    for (final recognizer in _recognizers) {
      recognizer.dispose();
    }
    _recognizers
      ..clear()
      ..addAll([
        for (var i = 0; i < (transcript?.segments.length ?? 0); i++) TapGestureRecognizer()..onTap = () => _choose(i),
      ]);
    _paragraphKeys
      ..clear()
      ..addAll([for (final _ in _paragraphs) GlobalKey()]);
  }

  @override
  void dispose() {
    for (final subscription in _subscriptions) {
      subscription.cancel();
    }
    for (final recognizer in _recognizers) {
      recognizer.dispose();
    }
    _player.dispose();
    _search.dispose();
    super.dispose();
  }

  void _onPosition(Duration position) {
    final segments = _transcript?.segments ?? const <Segment>[];
    final ms = position.inMilliseconds;
    final current = segments.indexWhere((s) => s.startMs <= ms && ms < s.endMs);
    final changed = current >= 0 && current != _current;
    setState(() {
      _position = position;
      if (current >= 0) _current = current;
    });
    if (changed && _follow && _playing) _showCurrent();
  }

  void _showCurrent() {
    final paragraph = _paragraphs.indexWhere((p) => p.segments.contains(_current));
    final context = paragraph >= 0 ? _paragraphKeys[paragraph].currentContext : null;
    if (context != null) {
      Scrollable.ensureVisible(context, alignment: 0.3, duration: const Duration(milliseconds: 200));
    }
  }

  /// Plays from a second before [startMs]: a segment occasionally starts
  /// later than its first word.
  Future<void> _playFrom(int startMs) async {
    await _player.seek(Duration(milliseconds: startMs > 1000 ? startMs - 1000 : 0));
    await _player.play();
  }

  void _choose(int segment) {
    setState(() {
      _selected = segment;
      _follow = true;
    });
    _playFrom(_transcript!.segments[segment].startMs);
  }

  String _speaker(AppLocalizations l, int id) => _transcript?.speakers[id] ?? l.speakerName('${id + 1}');

  Future<void> _copyText(AppLocalizations l) async {
    final text = Sillok.export(_transcript!.encode(), 'txt', l.speakerName('{n}'));
    await Clipboard.setData(ClipboardData(text: text));
    if (mounted) ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(l.copied)));
  }

  Future<void> _renameSpeakers(AppLocalizations l) async {
    final transcript = _transcript!;
    final names = await showDialog<Map<int, String>>(
      context: context,
      builder: (context) => _SpeakersDialog(
        speakers: {for (final id in transcript.speakers.keys) id: _speaker(l, id)},
        onListen: (id) {
          final first = transcript.segments.where((s) => s.speaker == id).firstOrNull;
          if (first != null) _playFrom(first.startMs);
        },
      ),
    );
    if (names == null) return;
    var renamed = transcript;
    for (final MapEntry(:key, :value) in names.entries) {
      renamed = renamed.renamed(key, value == l.speakerName('${key + 1}') ? null : value);
    }
    await widget.state.store.writeTranscript(_id, renamed);
    setState(() => _setTranscript(renamed));
  }

  void _showInfo(AppLocalizations l) {
    final meta = widget.entry.meta;
    final stages = _transcript?.stages ?? const {};
    final total = meta.timingsMs.values.fold(0, (a, b) => a + b);
    final transcription = stages['transcription'];
    showDialog<void>(
      context: context,
      builder: (context) => AlertDialog(
        title: Text(l.info),
        content: SingleChildScrollView(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            mainAxisSize: MainAxisSize.min,
            spacing: 4,
            children: [
              Text(l.infoLength(formatTime(meta.durationMs))),
              for (final step in RunStep.values)
                if (meta.timingsMs[step.name] case final ms?) Text(l.stepTime(stepName(l, step), formatTime(ms))),
              if (meta.durationMs > 0)
                Text(l.infoTotal(formatTime(total), (total / meta.durationMs).toStringAsFixed(2))),
              if (transcription != null)
                Text(
                  l.infoDevice(
                    transcription.fellBackToCpu ? l.infoFellBack(transcription.device) : transcription.device,
                  ),
                ),
              for (final MapEntry(:key, :value) in stages.entries) Text('$key: ${value.name}'),
            ],
          ),
        ),
        actions: [TextButton(onPressed: () => Navigator.of(context).pop(), child: Text(l.close))],
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    final l = AppLocalizations.of(context);
    final theme = Theme.of(context);
    final transcript = _transcript;
    if (!_loaded) return const Center(child: CircularProgressIndicator());
    if (transcript == null) return Center(child: Text(l.resultMissing));
    final query = _search.text.trim().toLowerCase();
    final shown = [
      for (final (i, p) in _paragraphs.indexed)
        if (query.isEmpty || p.segments.any((s) => transcript.segments[s].text.toLowerCase().contains(query))) i,
    ];
    final selected = _selected;
    return LayoutBuilder(
      builder: (context, constraints) {
        // The pane's own width: a narrow pane beside the list counts too.
        final narrow = constraints.maxWidth < 560;
        return _page(l, theme, transcript, shown, query, selected, narrow);
      },
    );
  }

  Widget _page(
    AppLocalizations l,
    ThemeData theme,
    Transcript transcript,
    List<int> shown,
    String query,
    int? selected,
    bool narrow,
  ) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Padding(
          padding: const EdgeInsetsDirectional.fromSTEB(16, 8, 4, 0),
          child: Row(
            children: [
              Expanded(
                child: Text(
                  widget.entry.meta.title,
                  style: theme.textTheme.titleLarge,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                ),
              ),
              IconButton(
                tooltip: l.search,
                isSelected: _searching,
                icon: const Icon(Icons.search),
                onPressed: () => setState(() {
                  _searching = !_searching;
                  if (!_searching) _search.clear();
                }),
              ),
              // Narrow screens keep only search as a button.
              if (!narrow) ...[
                if (transcript.speakers.isNotEmpty)
                  IconButton(
                    tooltip: l.speakers,
                    icon: const Icon(Icons.people_outline),
                    onPressed: () => _renameSpeakers(l),
                  ),
                IconButton(
                  tooltip: l.copyText,
                  icon: const Icon(Icons.copy_all_outlined),
                  onPressed: () => _copyText(l),
                ),
                IconButton(tooltip: l.info, icon: const Icon(Icons.info_outline), onPressed: () => _showInfo(l)),
              ],
              ResultMenu(
                state: widget.state,
                id: _id,
                title: widget.entry.meta.title,
                extra: narrow
                    ? [
                        if (transcript.speakers.isNotEmpty) (l.speakers, () => _renameSpeakers(l)),
                        (l.copyText, () => _copyText(l)),
                        (l.info, () => _showInfo(l)),
                      ]
                    : const [],
              ),
            ],
          ),
        ),
        if (_searching)
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 16),
            child: TextField(
              controller: _search,
              autofocus: true,
              decoration: InputDecoration(hintText: l.searchHint, prefixIcon: const Icon(Icons.search)),
              onChanged: (_) => setState(() {}),
            ),
          ),
        Expanded(
          child: transcript.segments.isEmpty
              ? Center(child: Text(l.resultEmpty))
              : shown.isEmpty
              ? Center(child: Text(l.searchNone))
              : Stack(
                  children: [
                    Positioned.fill(
                      // Only the reader's own scrolling has a direction; the
                      // view's moves to the playing sentence do not.
                      child: NotificationListener<UserScrollNotification>(
                        onNotification: (notification) {
                          if (notification.direction != ScrollDirection.idle && _follow) {
                            setState(() => _follow = false);
                          }
                          return false;
                        },
                        child: SingleChildScrollView(
                          padding: const EdgeInsets.symmetric(vertical: 8),
                          child: Column(
                            crossAxisAlignment: CrossAxisAlignment.stretch,
                            children: [for (final i in shown) _paragraph(l, theme, transcript, i, query)],
                          ),
                        ),
                      ),
                    ),
                    if (!_follow && _playing && _current != null)
                      PositionedDirectional(
                        start: 0,
                        end: 0,
                        bottom: 16,
                        child: Center(
                          child: FilledButton.tonalIcon(
                            onPressed: () {
                              setState(() => _follow = true);
                              _showCurrent();
                            },
                            icon: const Icon(Icons.graphic_eq),
                            label: Text(l.playingSentence),
                          ),
                        ),
                      ),
                  ],
                ),
        ),
        if (selected != null) _selectedCard(l, theme, transcript.segments[selected]),
        _playerBar(l, narrow),
      ],
    );
  }

  Widget _paragraph(AppLocalizations l, ThemeData theme, Transcript transcript, int index, String query) {
    final paragraph = _paragraphs[index];
    final speaker = paragraph.speaker;
    final first = transcript.segments[paragraph.segments.first];
    return Padding(
      key: _paragraphKeys[index],
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        spacing: 4,
        children: [
          Text(
            [if (speaker != null) _speaker(l, speaker), formatTime(first.startMs)].join(' · '),
            style: theme.textTheme.labelMedium?.copyWith(color: theme.colorScheme.primary),
          ),
          Text.rich(
            TextSpan(
              children: [
                for (final s in paragraph.segments) ...[
                  ..._segmentSpans(theme, transcript.segments[s].text, s, query),
                  const TextSpan(text: ' '),
                ],
              ],
            ),
            style: theme.textTheme.bodyLarge,
          ),
        ],
      ),
    );
  }

  /// A sentence, tappable, marked while it plays or is chosen, with search
  /// matches highlighted.
  List<TextSpan> _segmentSpans(ThemeData theme, String text, int segment, String query) {
    final background = segment == _selected
        ? theme.colorScheme.tertiaryContainer
        : segment == _current && _playing
        ? theme.colorScheme.secondaryContainer
        : null;
    final style = TextStyle(backgroundColor: background);
    final recognizer = _recognizers[segment];
    if (query.isEmpty) return [TextSpan(text: text, style: style, recognizer: recognizer)];
    final spans = <TextSpan>[];
    final lower = text.toLowerCase();
    var start = 0;
    for (var at = lower.indexOf(query); at >= 0; at = lower.indexOf(query, at + query.length)) {
      if (at > start) spans.add(TextSpan(text: text.substring(start, at), style: style, recognizer: recognizer));
      spans.add(
        TextSpan(
          text: text.substring(at, at + query.length),
          style: style.copyWith(backgroundColor: theme.colorScheme.primaryContainer, fontWeight: FontWeight.bold),
          recognizer: recognizer,
        ),
      );
      start = at + query.length;
    }
    if (start < text.length) spans.add(TextSpan(text: text.substring(start), style: style, recognizer: recognizer));
    return spans;
  }

  Widget _selectedCard(AppLocalizations l, ThemeData theme, Segment segment) => Card(
    margin: const EdgeInsets.symmetric(horizontal: 16, vertical: 4),
    child: Padding(
      padding: const EdgeInsetsDirectional.fromSTEB(16, 8, 8, 8),
      child: Row(
        children: [
          Expanded(
            child: Text(
              l.segmentInfo(
                formatPreciseTime(segment.startMs),
                formatPreciseTime(segment.endMs),
                languageName(l, segment.language),
              ),
            ),
          ),
          IconButton(
            tooltip: l.segmentPlay,
            icon: const Icon(Icons.play_arrow),
            onPressed: () => _playFrom(segment.startMs),
          ),
          IconButton(
            tooltip: l.segmentCopy,
            icon: const Icon(Icons.copy),
            onPressed: () async {
              await Clipboard.setData(ClipboardData(text: segment.text));
              if (mounted) ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(l.copied)));
            },
          ),
          IconButton(
            tooltip: l.close,
            icon: const Icon(Icons.close),
            onPressed: () => setState(() => _selected = null),
          ),
        ],
      ),
    ),
  );

  /// One row on wide screens; on narrow ones the slider gets its own.
  Widget _playerBar(AppLocalizations l, bool narrow) {
    final total = _duration.inMilliseconds > 0 ? _duration.inMilliseconds : widget.entry.meta.durationMs;
    final play = IconButton(
      tooltip: _playing ? l.pause : l.play,
      icon: Icon(_playing ? Icons.pause : Icons.play_arrow),
      onPressed: () => _playing ? _player.pause() : _player.play(),
    );
    final slider = Slider(
      value: _position.inMilliseconds.clamp(0, total > 0 ? total : 1).toDouble(),
      max: (total > 0 ? total : 1).toDouble(),
      onChanged: (value) => _player.seek(Duration(milliseconds: value.round())),
    );
    final time = Text(
      '${formatTime(_position.inMilliseconds)} / ${formatTime(total)}',
      maxLines: 1,
      overflow: TextOverflow.ellipsis,
    );
    return SafeArea(
      top: false,
      child: Padding(
        padding: const EdgeInsets.fromLTRB(8, 0, 16, 8),
        child: narrow
            ? Column(
                mainAxisSize: MainAxisSize.min,
                children: [
                  slider,
                  Row(
                    children: [
                      play,
                      Expanded(child: time),
                    ],
                  ),
                ],
              )
            : Row(
                children: [
                  play,
                  Expanded(child: slider),
                  time,
                ],
              ),
      ),
    );
  }
}

class _SpeakersDialog extends StatefulWidget {
  const _SpeakersDialog({required this.speakers, required this.onListen});

  final Map<int, String> speakers;
  final ValueChanged<int> onListen;

  @override
  State<_SpeakersDialog> createState() => _SpeakersDialogState();
}

class _SpeakersDialogState extends State<_SpeakersDialog> {
  late final _controllers = {
    for (final MapEntry(:key, :value) in widget.speakers.entries) key: TextEditingController(text: value),
  };

  @override
  void dispose() {
    for (final controller in _controllers.values) {
      controller.dispose();
    }
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final l = AppLocalizations.of(context);
    return AlertDialog(
      title: Text(l.speakers),
      content: SingleChildScrollView(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            for (final MapEntry(:key, :value) in _controllers.entries)
              Row(
                children: [
                  Expanded(child: TextField(controller: value)),
                  IconButton(
                    tooltip: l.speakerListen,
                    icon: const Icon(Icons.volume_up_outlined),
                    onPressed: () => widget.onListen(key),
                  ),
                ],
              ),
          ],
        ),
      ),
      actions: [
        TextButton(onPressed: () => Navigator.of(context).pop(), child: Text(l.cancel)),
        TextButton(
          onPressed: () => Navigator.of(context).pop({for (final e in _controllers.entries) e.key: e.value.text}),
          child: Text(l.save),
        ),
      ],
    );
  }
}
