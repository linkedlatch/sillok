import 'package:flutter/material.dart';

import '../app_state.dart';
import '../l10n/app_localizations.dart';
import 'format.dart';
import 'settings_fields.dart';

/// The running file: the current step's progress and time left, and the
/// sentences transcribed so far, followed as they arrive while the list is
/// at its end.
class RunningView extends StatefulWidget {
  const RunningView({super.key, required this.state});

  final AppState state;

  @override
  State<RunningView> createState() => _RunningViewState();
}

class _RunningViewState extends State<RunningView> {
  final _scroll = ScrollController();

  /// Whether the list is at its end, where new sentences keep it: scrolling
  /// up stops that and leaves the sentences being read in place; scrolling
  /// back down starts it again.
  final _follow = ValueNotifier(true);
  int _shown = 0;

  @override
  void initState() {
    super.initState();
    _scroll.addListener(() {
      final position = _scroll.position;
      _follow.value = position.pixels >= position.maxScrollExtent - 24;
    });
  }

  @override
  void dispose() {
    _scroll.dispose();
    _follow.dispose();
    super.dispose();
  }

  void _followNew(int count) {
    if (count == _shown) return;
    _shown = count;
    if (_follow.value) WidgetsBinding.instance.addPostFrameCallback((_) => _toEnd());
  }

  void _toEnd() {
    if (_scroll.hasClients) _scroll.jumpTo(_scroll.position.maxScrollExtent);
  }

  @override
  Widget build(BuildContext context) {
    final l = AppLocalizations.of(context);
    final theme = Theme.of(context);
    return ListenableBuilder(
      listenable: widget.state,
      builder: (context, _) {
        final run = widget.state.transcriber.run;
        if (run == null) return const SizedBox.shrink();
        final previews = widget.state.transcriber.previews;
        _followNew(previews.length);
        final header = Padding(
          padding: const EdgeInsets.all(16),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            spacing: 8,
            children: [
              Text(run.name, style: theme.textTheme.titleLarge, maxLines: 2, overflow: TextOverflow.ellipsis),
              Text(settingsSummary(l, widget.state, run.settings), style: theme.textTheme.bodySmall),
              LinearProgressIndicator(value: run.preparing ? null : run.stepFraction),
              Text(
                run.preparing
                    ? l.runningPreparing
                    : l.runningStep(
                        stepName(l, run.step),
                        run.stepNumber,
                        run.stepCount,
                        (run.stepFraction * 100).round(),
                      ),
              ),
              if (run.remaining != null) Text(l.runningLeft(formatTime(run.remaining!.inMilliseconds))),
              ExpansionTile(
                tilePadding: EdgeInsets.zero,
                title: Text(l.runningDetails),
                children: [
                  for (final MapEntry(:key, :value) in run.timingsMs.entries)
                    ListTile(dense: true, title: Text(l.stepTime(stepName(l, key), formatTime(value)))),
                ],
              ),
              Text(l.runningPreviews, style: theme.textTheme.bodySmall),
            ],
          ),
        );
        Widget preview(int i) => Padding(
          padding: const EdgeInsets.fromLTRB(16, 0, 16, 8),
          child: Text.rich(
            TextSpan(
              children: [
                TextSpan(text: '${formatTime(previews[i].startMs)}  ', style: theme.textTheme.bodySmall),
                TextSpan(text: previews[i].text),
              ],
            ),
          ),
        );
        final cancel = Padding(
          padding: const EdgeInsets.all(16),
          child: Align(
            alignment: AlignmentDirectional.centerEnd,
            child: Wrap(
              spacing: 8,
              runSpacing: 8,
              children: [
                OutlinedButton(
                  onPressed: run.stopping ? null : widget.state.transcriber.pause,
                  child: Text(run.pausing ? l.runningPausing : l.runningPause),
                ),
                OutlinedButton(
                  onPressed: run.stopping ? null : widget.state.transcriber.cancel,
                  child: Text(run.cancelling ? l.runningCancelling : l.runningCancel),
                ),
              ],
            ),
          ),
        );
        // Back to the newest sentence, while the list is scrolled away from it.
        Widget withLatest(Widget list) => Stack(
          children: [
            Positioned.fill(child: list),
            PositionedDirectional(
              start: 0,
              end: 0,
              bottom: 16,
              child: ValueListenableBuilder(
                valueListenable: _follow,
                builder: (context, follow, _) => follow
                    ? const SizedBox.shrink()
                    : Center(
                        child: FilledButton.tonalIcon(
                          onPressed: _toEnd,
                          icon: const Icon(Icons.arrow_downward),
                          label: Text(l.latest),
                        ),
                      ),
              ),
            ),
          ],
        );
        return LayoutBuilder(
          builder: (context, constraints) {
            // A short window scrolls the header away with the sentences; a
            // tall one keeps it in view.
            if (constraints.maxHeight < 560) {
              return withLatest(
                ListView.builder(
                  controller: _scroll,
                  itemCount: previews.length + 3,
                  itemBuilder: (context, i) => i == 0
                      ? header
                      : i == 1
                      ? const Divider(height: 1)
                      : i == previews.length + 2
                      ? cancel
                      : preview(i - 2),
                ),
              );
            }
            return Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                header,
                const Divider(height: 1),
                Expanded(
                  child: withLatest(
                    ListView.builder(
                      controller: _scroll,
                      padding: const EdgeInsets.only(top: 16),
                      itemCount: previews.length,
                      itemBuilder: (context, i) => preview(i),
                    ),
                  ),
                ),
                cancel,
              ],
            );
          },
        );
      },
    );
  }
}
