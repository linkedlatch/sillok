import 'dart:async';

import 'package:file_selector/file_selector.dart';
import 'package:flutter/material.dart';

import '../app_state.dart';
import '../data/media_types.dart';
import '../data/settings.dart';
import '../data/transcriber.dart';
import '../l10n/app_localizations.dart';
import 'format.dart';
import 'settings_fields.dart';
import 'settings_page.dart';
import 'transcript_view.dart';

/// What the detail shows: a queued file (by queue key) or a result (by job id).
sealed class Selection {
  const Selection();
}

final class QueuedSelection extends Selection {
  const QueuedSelection(this.key);

  final String key;

  @override
  bool operator ==(Object other) => other is QueuedSelection && other.key == key;

  @override
  int get hashCode => key.hashCode;
}

final class ResultSelection extends Selection {
  const ResultSelection(this.id);

  final String id;

  @override
  bool operator ==(Object other) => other is ResultSelection && other.id == id;

  @override
  int get hashCode => id.hashCode;
}

/// The library with the detail of the chosen file: side by side on wide
/// windows, the detail as its own page on narrow ones.
class HomePage extends StatefulWidget {
  const HomePage({super.key, required this.state});

  final AppState state;

  @override
  State<HomePage> createState() => _HomePageState();
}

class _HomePageState extends State<HomePage> {
  final _selection = ValueNotifier<Selection?>(null);
  late final StreamSubscription<(String, String)> _finished;

  /// On wide windows, the list folded away to give the detail the width.
  bool _listHidden = false;

  @override
  void initState() {
    super.initState();
    // A file that finishes while shown turns into its result.
    _finished = widget.state.finished.listen((done) {
      final (key, id) = done;
      if (_selection.value == QueuedSelection(key)) _selection.value = ResultSelection(id);
    });
  }

  @override
  void dispose() {
    _finished.cancel();
    _selection.dispose();
    super.dispose();
  }

  Future<void> _add() async {
    final files = await openFiles(acceptedTypeGroups: mediaTypes(AppLocalizations.of(context).mediaFiles));
    if (files.isEmpty || !mounted) return;
    await showModalBottomSheet<void>(
      context: context,
      isScrollControlled: true,
      showDragHandle: true,
      builder: (context) => AddSheet(state: widget.state, files: [for (final f in files) (f.path, f.name)]),
    );
  }

  void _open(Selection selection, bool wide) {
    _selection.value = selection;
    if (wide) return;
    Navigator.of(context).push(
      MaterialPageRoute<void>(
        builder: (context) => Scaffold(
          appBar: AppBar(),
          body: SafeArea(
            top: false,
            child: DetailPane(state: widget.state, selection: _selection, closeWhenGone: true),
          ),
        ),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    final l = AppLocalizations.of(context);
    return LayoutBuilder(
      builder: (context, constraints) {
        // Two panes from Material's medium width on; the list narrower below
        // the expanded width.
        final wide = constraints.maxWidth >= 600;
        final list = LibraryList(state: widget.state, selection: _selection, onOpen: (s) => _open(s, wide));
        final add = FloatingActionButton.extended(onPressed: _add, icon: const Icon(Icons.add), label: Text(l.add));
        final listHidden = wide && _listHidden;
        return Scaffold(
          appBar: AppBar(
            leading: wide
                ? IconButton(
                    tooltip: listHidden ? l.showList : l.hideList,
                    icon: Icon(listHidden ? Icons.menu : Icons.menu_open),
                    onPressed: () => setState(() => _listHidden = !_listHidden),
                  )
                : null,
            title: Text(l.appName),
            actions: [
              // The add button goes with the list.
              if (listHidden) IconButton(tooltip: l.add, icon: const Icon(Icons.add), onPressed: _add),
              IconButton(
                tooltip: l.settings,
                icon: const Icon(Icons.settings_outlined),
                onPressed: () =>
                    Navigator.of(context)
                        .push(MaterialPageRoute<void>(builder: (context) => SettingsPage(state: widget.state))),
              ),
            ],
          ),
          floatingActionButton: wide ? null : add,
          // Clear of the system's navigation bar and cutouts; the app bar
          // already keeps clear of the status bar.
          body: SafeArea(
            top: false,
            child: wide
                ? Row(
                    crossAxisAlignment: CrossAxisAlignment.stretch,
                    children: [
                      // Folding the list keeps the detail where it is, so an open
                      // result keeps its place.
                      if (!listHidden) ...[
                        // The add button over the list rather than the window's
                        // corner, where it would cover the detail's own buttons
                        // (a run's cancel).
                        SizedBox(
                          width: constraints.maxWidth >= 840 ? 380 : 320,
                          child: Stack(
                            children: [
                              Positioned.fill(child: list),
                              PositionedDirectional(
                                end: kFloatingActionButtonMargin,
                                bottom: kFloatingActionButtonMargin,
                                child: add,
                              ),
                            ],
                          ),
                        ),
                        const VerticalDivider(width: 1),
                      ],
                      Expanded(
                        child: DetailPane(state: widget.state, selection: _selection),
                      ),
                    ],
                  )
                : list,
          ),
        );
      },
    );
  }
}

/// The running, waiting and failed files, then the results, newest first.
class LibraryList extends StatelessWidget {
  const LibraryList({super.key, required this.state, required this.selection, required this.onOpen});

  final AppState state;
  final ValueNotifier<Selection?> selection;
  final ValueChanged<Selection> onOpen;

  @override
  Widget build(BuildContext context) {
    final l = AppLocalizations.of(context);
    return ListenableBuilder(
      listenable: Listenable.merge([state, selection]),
      builder: (context, _) {
        final queue = state.transcriber.queue;
        final run = state.transcriber.run;
        if (queue.isEmpty && state.library.isEmpty) {
          return Center(
            child: Padding(padding: const EdgeInsets.all(24), child: Text(l.homeEmpty)),
          );
        }
        return ListView(
          padding: const EdgeInsets.only(bottom: 88),
          children: [
            for (final item in queue)
              _QueueTile(
                state: state,
                item: item,
                run: run?.key == item.key ? run : null,
                selected: selection.value == QueuedSelection(item.key),
                onTap: () => onOpen(QueuedSelection(item.key)),
              ),
            for (final entry in state.library)
              ListTile(
                selected: selection.value == ResultSelection(entry.id),
                title: Text(entry.meta.title, maxLines: 1, overflow: TextOverflow.ellipsis),
                subtitle: Text('${formatDate(entry.meta.createdMs)} · ${formatTime(entry.meta.durationMs)}'),
                onTap: () => onOpen(ResultSelection(entry.id)),
                trailing: ResultMenu(state: state, id: entry.id, title: entry.meta.title),
              ),
          ],
        );
      },
    );
  }
}

class _QueueTile extends StatelessWidget {
  const _QueueTile({
    required this.state,
    required this.item,
    required this.run,
    required this.selected,
    required this.onTap,
  });

  final AppState state;
  final QueueItem item;
  final RunState? run;
  final bool selected;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final l = AppLocalizations.of(context);
    final run = this.run;
    final error = item.error;
    final subtitle = run != null
        ? Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            spacing: 4,
            children: [
              Text(
                run.preparing
                    ? l.runningPreparing
                    : run.remaining == null
                    ? l.libraryRunning(
                        stepName(l, run.step),
                        run.stepNumber,
                        run.stepCount,
                        (run.stepFraction * 100).round(),
                      )
                    : l.libraryRunningLeft(
                        stepName(l, run.step),
                        run.stepNumber,
                        run.stepCount,
                        (run.stepFraction * 100).round(),
                        formatTime(run.remaining!.inMilliseconds),
                      ),
              ),
              LinearProgressIndicator(value: run.preparing ? null : run.stepFraction),
            ],
          )
        : Text(
            error != null
                ? l.libraryFailed(errorText(l, error))
                : state.transcriber.preparing == item.key
                ? l.runningPreparing
                : item.paused
                ? (item.started ? l.libraryPaused : l.libraryLater)
                : l.libraryWaiting(state.transcriber.waitingPosition(item.key)),
            maxLines: 2,
            overflow: TextOverflow.ellipsis,
          );
    return ListTile(
      selected: selected,
      leading: Icon(
        error != null
            ? Icons.error_outline
            : run != null
            ? Icons.graphic_eq
            : item.paused
            ? Icons.pause_circle_outline
            : Icons.schedule,
      ),
      title: Text(item.name, maxLines: 1, overflow: TextOverflow.ellipsis),
      subtitle: subtitle,
      onTap: onTap,
      trailing: PopupMenuButton<String>(
        onSelected: (action) => switch (action) {
          'retry' => state.transcriber.retry(item.key),
          'unpause' => state.transcriber.unpause(item.key),
          'pause' => state.transcriber.pause(),
          _ => state.transcriber.remove(item.key),
        },
        itemBuilder: (context) => [
          if (error != null) PopupMenuItem(value: 'retry', child: Text(l.retry)),
          if (error == null && item.paused)
            PopupMenuItem(value: 'unpause', child: Text(item.started ? l.unpause : l.addStart)),
          if (run != null && !run.stopping) PopupMenuItem(value: 'pause', child: Text(l.runningPause)),
          PopupMenuItem(value: 'remove', child: Text(run != null ? l.runningCancel : l.remove)),
        ],
      ),
    );
  }
}

/// Rename and delete for a result, after [extra] actions (label, action)
/// that a narrow screen has no room to show as buttons.
class ResultMenu extends StatelessWidget {
  const ResultMenu({super.key, required this.state, required this.id, required this.title, this.extra = const []});

  final AppState state;
  final String id;
  final String title;
  final List<(String, VoidCallback)> extra;

  @override
  Widget build(BuildContext context) {
    final l = AppLocalizations.of(context);
    return PopupMenuButton<int>(
      onSelected: (index) async {
        if (index < extra.length) {
          extra[index].$2();
        } else if (index == extra.length) {
          final name = await askText(context, l.rename, l.title, title);
          if (name != null) await state.rename(id, name);
        } else if (await confirm(context, l.deleteConfirm(title), l.delete)) {
          await state.delete(id);
        }
      },
      itemBuilder: (context) => [
        for (final (i, (label, _)) in extra.indexed) PopupMenuItem(value: i, child: Text(label)),
        PopupMenuItem(value: extra.length, child: Text(l.rename)),
        PopupMenuItem(value: extra.length + 1, child: Text(l.delete)),
      ],
    );
  }
}

/// The detail of the selection; [closeWhenGone] leaves the page when the
/// file disappears (cancelled or deleted).
class DetailPane extends StatelessWidget {
  const DetailPane({super.key, required this.state, required this.selection, this.closeWhenGone = false});

  final AppState state;
  final ValueNotifier<Selection?> selection;
  final bool closeWhenGone;

  @override
  Widget build(BuildContext context) {
    final l = AppLocalizations.of(context);
    return ListenableBuilder(
      listenable: Listenable.merge([state, selection]),
      builder: (context, _) {
        Widget gone() {
          if (closeWhenGone) {
            WidgetsBinding.instance.addPostFrameCallback((_) {
              if (context.mounted) Navigator.of(context).maybePop();
            });
          }
          return Center(child: Text(l.detailNone));
        }

        switch (selection.value) {
          case null:
            return Center(child: Text(l.detailNone));
          case QueuedSelection(:final key):
            final item = state.transcriber.queue.where((i) => i.key == key).firstOrNull;
            if (item == null) {
              // A finished file becomes its result as soon as that is listed.
              final id = state.transcriber.finishedId(key);
              if (id == null) return gone();
              final entry = state.entry(id);
              // Until the result is listed, the queued view stays, with its
              // player; it shares the job folder's key with the result's.
              return entry == null
                  ? TranscriptView(key: ValueKey(id), state: state, queueKey: key, jobId: id)
                  : TranscriptView(key: ValueKey(id), state: state, entry: entry);
            }
            return TranscriptView(key: ValueKey(item.jobId ?? key), state: state, queueKey: key, jobId: item.jobId);
          case ResultSelection(:final id):
            final entry = state.entry(id);
            if (entry == null) return gone();
            return TranscriptView(key: ValueKey(id), state: state, entry: entry);
        }
      },
    );
  }
}

/// Files to transcribe: the defaults in a line, the options when opened.
class AddSheet extends StatefulWidget {
  const AddSheet({super.key, required this.state, required this.files});

  final AppState state;
  final List<(String, String)> files;

  @override
  State<AddSheet> createState() => _AddSheetState();
}

class _AddSheetState extends State<AddSheet> {
  late JobSettings _settings = widget.state.defaults;
  bool _options = false;
  bool _later = false;

  @override
  Widget build(BuildContext context) {
    final l = AppLocalizations.of(context);
    final state = widget.state;
    final ready = state.canRun(_settings);
    final missing = state.catalog.where(
      (m) => m.id == (_settings.transcriptionModel ?? state.recommendedTranscription.id),
    );
    return SafeArea(
      child: SingleChildScrollView(
        padding: EdgeInsets.fromLTRB(24, 0, 24, 24 + MediaQuery.viewInsetsOf(context).bottom),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          spacing: 12,
          children: [
            Text(l.addTitle(widget.files.length), style: Theme.of(context).textTheme.titleLarge),
            for (final (_, name) in widget.files.take(5)) Text(name, maxLines: 1, overflow: TextOverflow.ellipsis),
            if (widget.files.length > 5) const Text('…'),
            Text(settingsSummary(l, state, _settings), style: Theme.of(context).textTheme.bodyMedium),
            Align(
              alignment: AlignmentDirectional.centerStart,
              child: TextButton(onPressed: () => setState(() => _options = !_options), child: Text(l.addOptions)),
            ),
            if (_options)
              SettingsFields(state: state, settings: _settings, onChanged: (s) => setState(() => _settings = s)),
            if (!ready && missing.isNotEmpty)
              Text(
                l.addModelMissing(modelName(missing.first)),
                style: TextStyle(color: Theme.of(context).colorScheme.error),
              ),
            CheckboxListTile(
              contentPadding: EdgeInsets.zero,
              value: _later,
              onChanged: (value) => setState(() => _later = value ?? false),
              title: Text(l.addLater),
            ),
            FilledButton(
              onPressed: ready
                  ? () {
                      state.enqueue(widget.files, _settings, later: _later);
                      Navigator.of(context).pop();
                    }
                  : null,
              child: Text(_later ? l.addKeep : l.addStart),
            ),
          ],
        ),
      ),
    );
  }
}

Future<String?> askText(BuildContext context, String title, String label, String initial) => showDialog<String>(
  context: context,
  builder: (context) => _TextDialog(title: title, label: label, initial: initial),
);

class _TextDialog extends StatefulWidget {
  const _TextDialog({required this.title, required this.label, required this.initial});

  final String title;
  final String label;
  final String initial;

  @override
  State<_TextDialog> createState() => _TextDialogState();
}

class _TextDialogState extends State<_TextDialog> {
  late final _controller = TextEditingController(text: widget.initial);

  @override
  void dispose() {
    _controller.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final l = AppLocalizations.of(context);
    return AlertDialog(
      title: Text(widget.title),
      content: TextField(
        controller: _controller,
        autofocus: true,
        decoration: InputDecoration(labelText: widget.label),
        onSubmitted: (text) => Navigator.of(context).pop(text),
      ),
      actions: [
        TextButton(onPressed: () => Navigator.of(context).pop(), child: Text(l.cancel)),
        TextButton(onPressed: () => Navigator.of(context).pop(_controller.text), child: Text(l.save)),
      ],
    );
  }
}

Future<bool> confirm(BuildContext context, String message, String action) async =>
    await showDialog<bool>(
      context: context,
      builder: (context) {
        final l = AppLocalizations.of(context);
        return AlertDialog(
          content: Text(message),
          actions: [
            TextButton(onPressed: () => Navigator.of(context).pop(false), child: Text(l.cancel)),
            TextButton(onPressed: () => Navigator.of(context).pop(true), child: Text(action)),
          ],
        );
      },
    ) ??
    false;
