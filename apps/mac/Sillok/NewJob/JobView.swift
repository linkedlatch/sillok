import SillokEngine
import SwiftUI

struct JobView: View {
    let item: JobQueue.Item
    @Environment(JobQueue.self) private var queue
    @Environment(ModelManager.self) private var models
    @State private var following = true
    @State private var atBottom = true

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
                        StepRow(step: step, progress: progress(of: step),
                                remaining: step == item.step ? item.remaining : nil)
                    }
                }
                if item.state == .waiting {
                    Text("Number \(queue.position(of: item.id)) in line, after the files above it.")
                        .foregroundStyle(.secondary)
                }
                Button("Cancel") { queue.cancel(item.id) }
            }

            if !item.segments.isEmpty {
                Divider()
                ScrollViewReader { proxy in
                    ScrollView {
                        LazyVStack(alignment: .leading, spacing: 0) {
                            ForEach(Array(item.segments.enumerated()), id: \.offset) { index, segment in
                                HStack(alignment: .firstTextBaseline, spacing: 12) {
                                    Text(Timestamp.text(segment.startMs))
                                        .font(.callout.monospacedDigit())
                                        .foregroundStyle(.secondary)
                                    Text(segment.text)
                                        .textSelection(.enabled)
                                }
                                .padding(.vertical, 6)
                                .frame(maxWidth: .infinity, alignment: .leading)
                                .id(index)
                                Divider()
                            }
                        }
                    }
                    .onScrollGeometryChange(for: Bool.self) { geometry in
                        geometry.contentOffset.y + geometry.containerSize.height >= geometry.contentSize.height - 24
                    } action: { _, bottom in
                        atBottom = bottom
                    }
                    .onScrollPhaseChange { old, new in
                        if new == .interacting {
                            following = false
                        } else if new == .idle, old == .interacting || old == .decelerating {
                            following = atBottom
                        }
                    }
                    .overlay(alignment: .bottom) {
                        if !following {
                            Button("Latest", systemImage: "arrow.down") {
                                following = true
                                proxy.scrollTo(item.segments.count - 1, anchor: .bottom)
                            }
                            .buttonStyle(.borderedProminent)
                            .buttonBorderShape(.capsule)
                            .padding(.bottom, 12)
                        }
                    }
                    .onChange(of: item.segments.count) { _, count in
                        if following { proxy.scrollTo(count - 1, anchor: .bottom) }
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

    private func progress(of step: Step) -> Double? {
        guard let current = item.progress, let now = steps.firstIndex(of: current.step), let index = steps.firstIndex(of: step) else {
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
        case .stage(.transcribe): "Transcribing speech"
        case .stage(.align): "Timing words"
        case .stage(.diarize): "Identifying speakers"
        }
    }

    var key: String {
        switch self {
        case .adding: "copy"
        case .reading: "read"
        case .stage(.analyze): "analyze"
        case .stage(.transcribe): "transcribe"
        case .stage(.align): "align"
        case .stage(.diarize): "diarize"
        }
    }
}

private struct StepRow: View {
    let step: Step
    let progress: Double?
    let remaining: Duration?

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
                if let remaining {
                    Text("About \(remaining.formatted(.units(allowed: [.hours, .minutes, .seconds], width: .abbreviated, maximumUnitCount: 1))) left")
                        .font(.callout.monospacedDigit())
                        .foregroundStyle(.secondary)
                }
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
