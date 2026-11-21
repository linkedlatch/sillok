import SillokEngine
import SwiftUI

struct JobView: View {
    let item: JobQueue.Item
    @Environment(JobQueue.self) private var queue
    @Environment(ModelManager.self) private var models

    var body: some View {
        VStack(alignment: .leading, spacing: 20) {
            VStack(alignment: .leading, spacing: 4) {
                Text(item.title)
                    .font(.title2.bold())
                Text(item.sourceName)
                    .foregroundStyle(.secondary)
            }

            if case .failed(let message) = item.state {
                Label(message, systemImage: "exclamationmark.triangle.fill")
                    .foregroundStyle(.red)
                HStack {
                    Button("Try Again") { queue.retry(item.id) }
                    Button("Remove") { queue.cancel(item.id) }
                }
            } else {
                VStack(alignment: .leading, spacing: 10) {
                    ForEach(steps, id: \.self) { step in
                        StepRow(step: step, progress: progress(of: step))
                    }
                }
                Button("Cancel") { queue.cancel(item.id) }
            }

            if !item.segments.isEmpty {
                Divider()
                ScrollViewReader { proxy in
                    List(Array(item.segments.enumerated()), id: \.offset) { index, segment in
                        HStack(alignment: .firstTextBaseline, spacing: 12) {
                            Text(Timestamp.text(segment.startMs))
                                .font(.callout.monospacedDigit())
                                .foregroundStyle(.secondary)
                            Text(segment.text)
                                .textSelection(.enabled)
                        }
                        .id(index)
                    }
                    .listStyle(.plain)
                    .onChange(of: item.segments.count) { _, count in
                        proxy.scrollTo(count - 1, anchor: .bottom)
                    }
                }
            } else {
                Spacer()
            }
        }
        .padding(24)
        .frame(maxWidth: .infinity, alignment: .leading)
    }

    private var steps: [Step] {
        var steps: [Step] = [.adding, .reading, .stage(.analyze), .stage(.transcribe)]
        if models.transcriptionModels.first(where: { $0.id == item.modelID })?.aligner == true {
            steps.append(.stage(.align))
        }
        if item.options.diarize { steps.append(.stage(.diarize)) }
        return steps
    }

    private var current: (step: Step, fraction: Double)? {
        switch item.state {
        case .failed: nil
        case .adding(let fraction): (.adding, fraction)
        case .waiting: (.adding, 1)
        case .reading(let fraction): (.reading, fraction)
        case .running(let stage, let fraction): (.stage(stage), fraction)
        }
    }

    private func progress(of step: Step) -> Double? {
        guard let current, let now = steps.firstIndex(of: current.step), let index = steps.firstIndex(of: step) else {
            return nil
        }
        if index < now { return 1 }
        if index == now { return current.fraction }
        return nil
    }
}

enum Step: Hashable {
    case adding
    case reading
    case stage(Stage)

    var title: LocalizedStringKey {
        switch self {
        case .adding: "Adding the file"
        case .reading: "Reading the file"
        case .stage(.analyze): "Finding speech"
        case .stage(.transcribe): "Transcribing"
        case .stage(.align): "Timing words"
        case .stage(.diarize): "Identifying speakers"
        }
    }
}

private struct StepRow: View {
    let step: Step
    let progress: Double?

    var body: some View {
        HStack(spacing: 10) {
            Group {
                if progress == 1 {
                    Image(systemName: "checkmark.circle.fill").foregroundStyle(.green)
                } else if progress != nil {
                    ProgressView().controlSize(.small)
                } else {
                    Image(systemName: "circle").foregroundStyle(.tertiary)
                }
            }
            .frame(width: 18)
            Text(step.title)
                .foregroundStyle(progress == nil ? .secondary : .primary)
            if let progress, progress > 0, progress < 1 {
                ProgressView(value: progress)
                    .frame(width: 160)
                Text(progress.formatted(.percent.precision(.fractionLength(0))))
                    .font(.callout.monospacedDigit())
                    .foregroundStyle(.secondary)
            }
        }
    }
}

enum Timestamp {
    static func text(_ milliseconds: Int) -> String {
        let duration = Duration.milliseconds(milliseconds)
        let pattern: Duration.TimeFormatStyle.Pattern = milliseconds >= 3_600_000 ? .hourMinuteSecond : .minuteSecond
        return duration.formatted(.time(pattern: pattern))
    }
}
