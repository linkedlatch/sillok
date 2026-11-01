import 'package:flutter/material.dart';

import '../app_state.dart';
import '../l10n/app_localizations.dart';
import 'format.dart';
import 'settings_fields.dart';

/// The running file: progress over all steps, the time left, and the
/// sentences transcribed so far, followed as they arrive.
class RunningView extends StatefulWidget {
  const RunningView({super.key, required this.state});

  final AppState state;

  @override
  State<RunningView> createState() => _RunningViewState();
}

class _RunningViewState extends State<RunningView> {
  final _scroll = ScrollController();
  bool _follow = true;
  int _shown = 0;

  @override
  void dispose() {
    _scroll.dispose();
    super.dispose();
  }

  void _followNew(int count) {
    if (count == _shown) return;
    _shown = count;
    if (!_follow) return;
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (_scroll.hasClients) _scroll.jumpTo(_scroll.position.maxScrollExtent);
    });
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
        return Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Padding(
              padding: const EdgeInsets.all(16),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                spacing: 8,
                children: [
                  Text(run.name, style: theme.textTheme.titleLarge, maxLines: 2, overflow: TextOverflow.ellipsis),
                  Text(settingsSummary(l, widget.state, run.settings), style: theme.textTheme.bodySmall),
                  LinearProgressIndicator(value: run.overall),
                  Text(l.runningOverall(stepName(l, run.step), (run.overall * 100).round())),
                  if (run.remaining != null) Text(l.runningLeft(formatTime(run.remaining!.inMilliseconds))),
                  ExpansionTile(
                    tilePadding: EdgeInsets.zero,
                    title: Text(l.runningDetails),
                    children: [
                      for (final MapEntry(:key, :value) in run.timingsMs.entries)
                        ListTile(dense: true, title: Text(l.stepTime(stepName(l, key), formatTime(value)))),
                    ],
                  ),
                  Row(
                    children: [
                      Expanded(child: Text(l.runningPreviews, style: theme.textTheme.bodySmall)),
                      FilterChip(
                        label: Text(l.follow),
                        selected: _follow,
                        onSelected: (value) => setState(() => _follow = value),
                      ),
                    ],
                  ),
                ],
              ),
            ),
            const Divider(height: 1),
            Expanded(
              child: ListView.builder(
                controller: _scroll,
                padding: const EdgeInsets.all(16),
                itemCount: previews.length,
                itemBuilder: (context, i) => Padding(
                  padding: const EdgeInsets.only(bottom: 8),
                  child: Text.rich(TextSpan(children: [
                    TextSpan(text: '${formatTime(previews[i].startMs)}  ', style: theme.textTheme.bodySmall),
                    TextSpan(text: previews[i].text),
                  ])),
                ),
              ),
            ),
            Padding(
              padding: const EdgeInsets.all(16),
              child: Align(
                alignment: AlignmentDirectional.centerEnd,
                child: OutlinedButton(
                  onPressed: run.cancelling ? null : widget.state.transcriber.cancel,
                  child: Text(run.cancelling ? l.runningCancelling : l.runningCancel),
                ),
              ),
            ),
          ],
        );
      },
    );
  }
}
