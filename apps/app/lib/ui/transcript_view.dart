import 'dart:async';

import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter/services.dart';

import '../app_state.dart';
import '../data/store.dart';
import '../data/transcriber.dart';
import '../data/transcript.dart';
import '../engine/job_run.dart';
import '../engine/sillok.dart';
import '../l10n/app_localizations.dart';
import 'format.dart';
import 'home.dart';
import 'playback.dart';
import 'settings_fields.dart';

/// A file's text over a player (DESIGN D36): a finished result, or a file in
/// the queue with what is transcribed so far, while it transcribes or waits
/// paused. Choosing a sentence plays it from a second before and shows what is
/// otherwise hidden: its exact times and language. Dragging the player's
/// slider shows the sentence at that time, and letting go goes to it.
///
/// A queued file and its result share their job folder, which keys this view,
/// so the view stays, with its player, as the file turns into its result.
class TranscriptView extends StatefulWidget {
  const TranscriptView({super.key, required this.state, this.entry, this.queueKey, this.jobId})
    : assert(entry != null || queueKey != null);

  final AppState state;

  /// A finished result.
  final LibraryEntry? entry;

  /// A file in the queue, while [entry] is null.
  final String? queueKey;

  /// The queued file's job folder, once it has one; also once it left the
  /// queue and its result is not listed yet.
  final String? jobId;

  @override
  State<TranscriptView> createState() => _TranscriptViewState();
}

class _TranscriptViewState extends State<TranscriptView> {
  late final Playback _player = widget.state.newPlayback();
  final _subscriptions = <StreamSubscription<Object?>>[];
  final _recognizers = <TapGestureRecognizer>[];
  final _paragraphKeys = <GlobalKey>[];
  final _textKeys = <GlobalKey>[]; // each paragraph's text, to find a sentence in it
  final _search = TextEditingController();
  final _scroll = ScrollController();

  /// The finished transcript; null while the file is in the queue.
  Transcript? _transcript;
  List<Segment> _segments = const [];
  List<Paragraph> _paragraphs = const [];

  /// What a stopped run left in the checkpoint, shown while the file waits
  /// and until a continued run sends it again.
  List<Segment> _saved = const [];
  bool _loaded = false;
  bool _playing = false;
  bool _searching = false;
  String? _audio; // the copy, once opened
  bool _wasRunning = false;

  /// Whether the playing sentence's paragraph is scrolled out of view. The
  /// text never moves by itself during playback; a button goes to the
  /// playing sentence instead.
  bool _currentHidden = false;

  /// Whether the text is at its end, where new sentences keep it while the
  /// file transcribes: scrolling up stops that, scrolling back down starts it.
  bool _following = true;
  final _textKey = GlobalKey();
  Duration _position = Duration.zero;
  Duration _duration = Duration.zero;
  int? _selected;
  int? _current;

  /// The sentence the slider was let go at, marked until playback moves on.
  int? _landed;

  /// Where the slider is being dragged, in milliseconds.
  double? _dragging;

  QueueItem? get _item =>
      widget.entry != null ? null : widget.state.transcriber.queue.where((i) => i.key == widget.queueKey).firstOrNull;

  RunState? get _run {
    final run = widget.state.transcriber.run;
    return widget.entry == null && run != null && run.key == widget.queueKey ? run : null;
  }

  String? get _jobId => widget.entry?.id ?? _item?.jobId ?? widget.jobId;

  String get _title {
    if (widget.entry case final entry?) return entry.meta.title;
    final name = _item?.name ?? '';
    final dot = name.lastIndexOf('.');
    return dot > 0 ? name.substring(0, dot) : name;
  }

  @override
  void initState() {
    super.initState();
    _subscriptions
      ..add(_player.position.listen(_onPosition))
      ..add(_player.duration.listen((d) => setState(() => _duration = d)))
      ..add(_player.playing.listen((playing) => setState(() => _playing = playing)));
    _scroll.addListener(() {
      final position = _scroll.position;
      final following = position.pixels >= position.maxScrollExtent - 24;
      if (following != _following) setState(() => _following = following);
    });
    widget.state.addListener(_onState);
    _load();
  }

  @override
  void didUpdateWidget(TranscriptView old) {
    super.didUpdateWidget(old);
    // The file finished: its result replaces the sentences so far.
    if (old.entry == null && widget.entry != null) _load();
  }

  Future<void> _load() async {
    await _openAudio();
    final entry = widget.entry;
    if (entry != null) {
      final transcript = await widget.state.store.readTranscript(entry.id);
      if (!mounted) return;
      setState(() {
        _transcript = transcript;
        _selected = null;
        _current = null;
        _landed = null;
        _setSegments(transcript?.segments ?? const []);
        _loaded = true;
      });
    } else {
      await _loadSaved();
      if (mounted) setState(() => _loaded = true);
    }
  }

  /// Opens the copy for listening back once it is saved.
  Future<void> _openAudio() async {
    final id = _jobId;
    if (id == null || _audio != null) return;
    final audio = widget.state.store.audioOf(id);
    if (!audio.existsSync()) return;
    _audio = audio.path;
    await _player.open(audio.path);
    if (mounted) setState(() {});
  }

  Future<void> _loadSaved() async {
    final id = _jobId;
    final saved = id == null ? const <Segment>[] : await widget.state.store.readCheckpoint(id);
    if (!mounted) return;
    setState(() {
      _saved = saved;
      _showQueued();
    });
  }

  void _onState() {
    if (!mounted || widget.entry != null) return;
    unawaited(_openAudio());
    final running = _run != null;
    if (_wasRunning && !running && _item != null) unawaited(_loadSaved()); // paused or failed
    _wasRunning = running;
    // Gone from the queue: it finished, and its result follows.
    if (_item == null) return;
    setState(_showQueued);
  }

  /// The sentences of a queued file: those of its run, or those saved.
  void _showQueued() {
    final previews = widget.state.transcriber.previews;
    final segments = _run != null && previews.isNotEmpty ? previews : _saved;
    // The same sentences: the queue's lists are copies of the same segments.
    if (segments.length == _segments.length && (segments.isEmpty || identical(segments.last, _segments.last))) return;
    final grew = segments.length > _segments.length;
    _setSegments(segments);
    if (grew && _following && _run != null) {
      WidgetsBinding.instance.addPostFrameCallback((_) => _toEnd());
    }
  }

  void _setSegments(List<Segment> segments) {
    _segments = segments;
    _paragraphs = paragraphs(segments);
    while (_recognizers.length > segments.length) {
      _recognizers.removeLast().dispose();
    }
    for (var i = _recognizers.length; i < segments.length; i++) {
      _recognizers.add(TapGestureRecognizer()..onTap = () => _choose(i));
    }
    if (_paragraphKeys.length > _paragraphs.length) {
      _paragraphKeys.removeRange(_paragraphs.length, _paragraphKeys.length);
      _textKeys.removeRange(_paragraphs.length, _textKeys.length);
    }
    while (_paragraphKeys.length < _paragraphs.length) {
      _paragraphKeys.add(GlobalKey());
      _textKeys.add(GlobalKey());
    }
    if (_selected != null && _selected! >= segments.length) _selected = null;
    if (_current != null && _current! >= segments.length) _current = null;
  }

  @override
  void dispose() {
    widget.state.removeListener(_onState);
    for (final subscription in _subscriptions) {
      subscription.cancel();
    }
    for (final recognizer in _recognizers) {
      recognizer.dispose();
    }
    _player.dispose();
    _search.dispose();
    _scroll.dispose();
    super.dispose();
  }

  /// Where a jump to [ms] lands (DESIGN D36).
  int _landing(int ms) => landingSegment(_segments, ms, complete: widget.entry != null);

  /// Where [segment] is on the screen, if its paragraph is shown.
  Rect? _sentenceRect(int segment) {
    final index = _paragraphs.indexWhere((p) => p.segments.contains(segment));
    final render = index >= 0 ? _textKeys[index].currentContext?.findRenderObject() : null;
    if (render is! RenderParagraph || !render.attached) return null;
    // The paragraph's text is its sentences, each followed by a space.
    var offset = 0;
    for (final s in _paragraphs[index].segments) {
      if (s == segment) break;
      offset += _segments[s].text.length + 1;
    }
    final boxes = render.getBoxesForSelection(
      TextSelection(baseOffset: offset, extentOffset: offset + _segments[segment].text.length),
    );
    if (boxes.isEmpty) return null;
    var rect = boxes.first.toRect();
    for (final box in boxes.skip(1)) {
      rect = rect.expandToInclude(box.toRect());
    }
    return MatrixUtils.transformRect(render.getTransformTo(null), rect);
  }

  Rect? get _viewRect {
    final view = _textKey.currentContext?.findRenderObject();
    return view is RenderBox && view.attached ? view.localToGlobal(Offset.zero) & view.size : null;
  }

  /// Whether [segment] passes the search, so that its paragraph is shown.
  bool _inSearch(int segment) {
    final query = _search.text.trim().toLowerCase();
    if (query.isEmpty) return true;
    final paragraph = _paragraphs.where((p) => p.segments.contains(segment)).firstOrNull;
    return paragraph != null && paragraph.segments.any((s) => _segments[s].text.toLowerCase().contains(query));
  }

  void _onPosition(Duration position) {
    final ms = position.inMilliseconds;
    final current = _segments.indexWhere((s) => s.startMs <= ms && ms < s.endMs);
    final changed = current >= 0 && current != _current;
    setState(() {
      _position = position;
      if (current >= 0) _current = current;
      if (changed && current != _landed) _landed = null;
    });
    if (changed) WidgetsBinding.instance.addPostFrameCallback((_) => _checkCurrent());
  }

  void _checkCurrent() {
    if (!mounted) return;
    final current = _current;
    final rect = current == null ? null : _sentenceRect(current);
    final view = _viewRect;
    final hidden = rect != null && view != null && !rect.overlaps(view);
    if (hidden != _currentHidden) setState(() => _currentHidden = hidden);
  }

  /// Scrolls [segment] to a third of the way down the text; with [ifHidden],
  /// only when it is not in view already.
  void _showSegment(int? segment, {bool ifHidden = false}) {
    final rect = segment == null ? null : _sentenceRect(segment);
    final view = _viewRect;
    if (rect == null || view == null || !_scroll.hasClients) return;
    final inView = rect.top >= view.top && (rect.bottom <= view.bottom || rect.height > view.height);
    if (ifHidden && inView && rect.top < view.bottom) return;
    final position = _scroll.position;
    final target = (position.pixels + rect.top - view.top - view.height / 3).clamp(
      position.minScrollExtent,
      position.maxScrollExtent,
    );
    _scroll.animateTo(target, duration: const Duration(milliseconds: 200), curve: Curves.easeOut);
  }

  void _toEnd() {
    if (_scroll.hasClients) _scroll.jumpTo(_scroll.position.maxScrollExtent);
  }

  /// Plays from a second before [startMs]: a segment occasionally starts
  /// later than its first word.
  Future<void> _playFrom(int startMs) async {
    await _player.seek(Duration(milliseconds: startMs > 1000 ? startMs - 1000 : 0));
    await _player.play();
  }

  void _choose(int segment) {
    setState(() => _selected = segment);
    _playFrom(_segments[segment].startMs);
  }

  /// The slider let go at [ms]: plays from there and goes to its sentence,
  /// scrolling only when it is out of view. Past what is transcribed, or with
  /// the sentence left out by the search, only the playback moves.
  void _land(double ms) {
    final segment = _landing(ms.round());
    final goes = segment >= 0 && _inSearch(segment);
    setState(() {
      _dragging = null;
      _position = Duration(milliseconds: ms.round());
      if (goes) {
        _current = segment;
        _landed = segment;
      }
    });
    _player.seek(Duration(milliseconds: ms.round()));
    if (goes) WidgetsBinding.instance.addPostFrameCallback((_) => _showSegment(segment, ifHidden: true));
  }

  String _speaker(AppLocalizations l, int id) => _transcript?.speakers[id] ?? l.speakerName('${id + 1}');

  Future<void> _copyText(AppLocalizations l) async {
    final transcript =
        _transcript ??
        Transcript({
          'version': 1,
          'duration_ms': _duration.inMilliseconds,
          'speakers': const [],
          'segments': [
            for (final s in _segments)
              {'start_ms': s.startMs, 'end_ms': s.endMs, 'text': s.text, 'language': s.language, 'words': const []},
          ],
          'models': const {},
        });
    final text = Sillok.export(transcript.encode(), 'txt', l.speakerName('{n}'));
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
    await widget.state.store.writeTranscript(widget.entry!.id, renamed);
    setState(() {
      _transcript = renamed;
      _setSegments(renamed.segments);
    });
  }

  void _showInfo(AppLocalizations l) {
    final meta = widget.entry!.meta;
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
    if (!_loaded) return const Center(child: CircularProgressIndicator());
    if (widget.entry != null && _transcript == null) return Center(child: Text(l.resultMissing));
    final query = _search.text.trim().toLowerCase();
    final shown = [
      for (final (i, p) in _paragraphs.indexed)
        if (query.isEmpty || p.segments.any((s) => _segments[s].text.toLowerCase().contains(query))) i,
    ];
    return LayoutBuilder(
      builder: (context, constraints) {
        // The pane's own width: a narrow pane beside the list counts too.
        final narrow = constraints.maxWidth < 560;
        return _page(l, theme, shown, query, narrow);
      },
    );
  }

  Widget _page(AppLocalizations l, ThemeData theme, List<int> shown, String query, bool narrow) {
    final entry = widget.entry;
    final item = _item;
    final transcript = _transcript;
    final selected = _selected;
    final hasText = _segments.isNotEmpty;
    final speakers = transcript != null && transcript.speakers.isNotEmpty;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Padding(
          padding: const EdgeInsetsDirectional.fromSTEB(16, 8, 4, 0),
          child: Row(
            children: [
              Expanded(
                child: Text(_title, style: theme.textTheme.titleLarge, maxLines: 1, overflow: TextOverflow.ellipsis),
              ),
              if (hasText)
                IconButton(
                  tooltip: l.search,
                  isSelected: _searching,
                  icon: const Icon(Icons.search),
                  onPressed: () => setState(() {
                    _searching = !_searching;
                    if (!_searching) _search.clear();
                  }),
                ),
              // Narrow screens keep only search as a button; a queued file has
              // only its text to copy.
              if (hasText && (!narrow || entry == null))
                IconButton(
                  tooltip: l.copyText,
                  icon: const Icon(Icons.copy_all_outlined),
                  onPressed: () => _copyText(l),
                ),
              if (entry != null) ...[
                if (!narrow) ...[
                  if (speakers)
                    IconButton(
                      tooltip: l.speakers,
                      icon: const Icon(Icons.people_outline),
                      onPressed: () => _renameSpeakers(l),
                    ),
                  IconButton(tooltip: l.info, icon: const Icon(Icons.info_outline), onPressed: () => _showInfo(l)),
                ],
                ResultMenu(
                  state: widget.state,
                  id: entry.id,
                  title: entry.meta.title,
                  extra: narrow
                      ? [
                          if (speakers) (l.speakers, () => _renameSpeakers(l)),
                          if (hasText) (l.copyText, () => _copyText(l)),
                          (l.info, () => _showInfo(l)),
                        ]
                      : const [],
                ),
              ],
            ],
          ),
        ),
        if (item != null) _status(l, theme, item),
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
          child: !hasText
              ? Center(
                  child: Padding(
                    padding: const EdgeInsets.all(16),
                    child: Text(entry != null ? l.resultEmpty : l.transcriptNone, textAlign: TextAlign.center),
                  ),
                )
              : shown.isEmpty
              ? Center(child: Text(l.searchNone))
              : Stack(
                  children: [
                    Positioned.fill(
                      key: _textKey,
                      child: NotificationListener<ScrollNotification>(
                        onNotification: (_) {
                          _checkCurrent();
                          return false;
                        },
                        child: SingleChildScrollView(
                          controller: _scroll,
                          padding: const EdgeInsets.symmetric(vertical: 8),
                          child: Column(
                            crossAxisAlignment: CrossAxisAlignment.stretch,
                            children: [for (final i in shown) _paragraph(l, theme, i, query)],
                          ),
                        ),
                      ),
                    ),
                    PositionedDirectional(
                      start: 0,
                      end: 0,
                      bottom: 16,
                      child: Center(
                        child: Wrap(
                          spacing: 8,
                          runSpacing: 8,
                          children: [
                            if (_currentHidden && _playing)
                              FilledButton.tonalIcon(
                                onPressed: () => _showSegment(_current),
                                icon: const Icon(Icons.graphic_eq),
                                label: Text(l.playingSentence),
                              ),
                            // Back to the newest sentence, while the text is
                            // scrolled away from it.
                            if (_run != null && !_following && query.isEmpty)
                              FilledButton.tonalIcon(
                                onPressed: _toEnd,
                                icon: const Icon(Icons.arrow_downward),
                                label: Text(l.latest),
                              ),
                          ],
                        ),
                      ),
                    ),
                  ],
                ),
        ),
        if (selected != null) _selectedCard(l, _segments[selected]),
        if (entry != null || _audio != null) _playerBar(l, theme, narrow),
      ],
    );
  }

  /// Where a queued file stands, with what can be done about it.
  Widget _status(AppLocalizations l, ThemeData theme, QueueItem item) {
    final transcriber = widget.state.transcriber;
    final run = _run;
    final error = item.error;
    final preparing = transcriber.preparing == item.key;
    final String text;
    if (run != null) {
      final step = run.preparing
          ? l.runningPreparing
          : l.runningStep(stepName(l, run.step), run.stepNumber, run.stepCount, (run.stepFraction * 100).round());
      text = run.remaining == null ? step : '$step · ${l.runningLeft(formatTime(run.remaining!.inMilliseconds))}';
    } else if (error != null) {
      text = errorText(l, error);
    } else if (preparing) {
      text = l.detailPreparing;
    } else if (item.paused) {
      text = item.started ? l.detailPaused : l.detailLater;
    } else {
      text = l.detailWaiting(transcriber.waitingPosition(item.key));
    }
    return Padding(
      padding: const EdgeInsets.fromLTRB(16, 4, 16, 4),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        spacing: 6,
        children: [
          if (run != null || preparing)
            LinearProgressIndicator(value: run == null || run.preparing ? null : run.stepFraction),
          Text(text),
          Text(
            [settingsSummary(l, widget.state, item.settings), if (_segments.isNotEmpty) l.runningPreviews].join(' · '),
            style: theme.textTheme.bodySmall,
          ),
          Wrap(
            spacing: 8,
            runSpacing: 8,
            children: [
              if (run != null) ...[
                OutlinedButton(
                  onPressed: run.stopping ? null : transcriber.pause,
                  child: Text(run.pausing ? l.runningPausing : l.runningPause),
                ),
                OutlinedButton(
                  onPressed: run.stopping ? null : transcriber.cancel,
                  child: Text(run.cancelling ? l.runningCancelling : l.runningCancel),
                ),
              ] else ...[
                if (error != null)
                  FilledButton(onPressed: () => transcriber.retry(item.key), child: Text(l.retry))
                else if (item.paused)
                  FilledButton(
                    onPressed: () => transcriber.unpause(item.key),
                    child: Text(item.started ? l.unpause : l.addStart),
                  ),
                OutlinedButton(onPressed: () => transcriber.remove(item.key), child: Text(l.remove)),
              ],
            ],
          ),
        ],
      ),
    );
  }

  Widget _paragraph(AppLocalizations l, ThemeData theme, int index, String query) {
    final paragraph = _paragraphs[index];
    // The speaker where the turn starts; search results show it on each.
    final speaker = query.isEmpty && index > 0 && _paragraphs[index - 1].speaker == paragraph.speaker
        ? null
        : paragraph.speaker;
    final first = _segments[paragraph.segments.first];
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
            key: _textKeys[index],
            TextSpan(
              children: [
                for (final s in paragraph.segments) ...[
                  ..._segmentSpans(theme, _segments[s].text, s, query),
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

  /// A sentence, tappable, marked while it plays, is chosen or was gone to
  /// with the slider, with search matches highlighted.
  List<TextSpan> _segmentSpans(ThemeData theme, String text, int segment, String query) {
    final background = segment == _selected
        ? theme.colorScheme.tertiaryContainer
        : segment == _current && (_playing || segment == _landed)
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

  Widget _selectedCard(AppLocalizations l, Segment segment) => Card(
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

  /// One row on wide screens; on narrow ones the slider gets its own. While
  /// the slider is dragged, the sentence at its time shows above it.
  Widget _playerBar(AppLocalizations l, ThemeData theme, bool narrow) {
    final known = _duration.inMilliseconds > 0 ? _duration.inMilliseconds : widget.entry?.meta.durationMs ?? 0;
    final total = known > 0 ? known : 1;
    final dragging = _dragging;
    final at = dragging ?? _position.inMilliseconds.clamp(0, total).toDouble();
    final play = IconButton(
      tooltip: _playing ? l.pause : l.play,
      icon: Icon(_playing ? Icons.pause : Icons.play_arrow),
      onPressed: () => _playing ? _player.pause() : _player.play(),
    );
    final slider = Slider(
      value: at.clamp(0, total.toDouble()),
      max: total.toDouble(),
      onChangeStart: (value) => setState(() => _dragging = value),
      onChanged: (value) => setState(() => _dragging = value),
      onChangeEnd: _land,
    );
    final time = Text('${formatTime(at.round())} / ${formatTime(known)}', maxLines: 1, overflow: TextOverflow.ellipsis);
    final segment = dragging == null ? -1 : _landing(dragging.round());
    return SafeArea(
      top: false,
      child: Padding(
        padding: const EdgeInsets.fromLTRB(8, 0, 16, 8),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            if (dragging != null)
              Card(
                key: const ValueKey('seek preview'),
                margin: const EdgeInsetsDirectional.only(start: 8, top: 4),
                child: Padding(
                  padding: const EdgeInsets.all(8),
                  child: Text(
                    segment >= 0
                        ? '${formatTime(dragging.round())}  ${_segments[segment].text}'
                        : formatTime(dragging.round()),
                    maxLines: 2,
                    overflow: TextOverflow.ellipsis,
                  ),
                ),
              ),
            if (narrow) ...[
              slider,
              Row(
                children: [
                  play,
                  Expanded(child: time),
                ],
              ),
            ] else
              Row(
                children: [
                  play,
                  Expanded(child: slider),
                  time,
                ],
              ),
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
