import SillokEngine
import SillokStore
import SwiftUI

struct RecordingInfoView: View {
    let recording: Recording
    let transcript: Transcript?

    var body: some View {
        Grid(alignment: .leadingFirstTextBaseline, horizontalSpacing: 12, verticalSpacing: 6) {
            row("Length", Timestamp.text(recording.durationMs ?? 0))
            row("Added", recording.createdAt.formatted(date: .abbreviated, time: .shortened))
            if let transcribedAt = recording.transcribedAt {
                row("Transcribed", transcribedAt.formatted(date: .abbreviated, time: .shortened))
            }
            if let model = transcript?.models["transcription"] {
                row("Model", model.name)
                row("Device", model.device)
                if model.fellBackToCpu == true {
                    GridRow {
                        Color.clear.gridCellUnsizedAxes([.horizontal, .vertical])
                        Label("The GPU failed, so this ran on the CPU.", systemImage: "exclamationmark.triangle")
                            .foregroundStyle(.orange)
                    }
                }
            }
            if !times.isEmpty {
                Divider().gridCellUnsizedAxes(.horizontal)
                ForEach(times, id: \.step) { time in
                    row(time.step.title, Self.format(time.milliseconds))
                }
                row("Total", Self.format(total))
                if let durationMs = recording.durationMs, total > 0 {
                    let speed = Double(durationMs) / Double(total)
                    row("Speed", String(localized: "\(speed.formatted(.number.precision(.fractionLength(1))))× real time"))
                }
            }
        }
        .padding(16)
    }

    private var times: [(step: Step, milliseconds: Int)] {
        let steps: [Step] = [.adding, .reading, .stage(.analyze), .stage(.transcribe), .stage(.align), .stage(.diarize)]
        return steps.compactMap { step in recording.timingsMs?[step.key].map { (step, $0) } }
    }

    private var total: Int {
        times.map(\.milliseconds).reduce(0, +)
    }

    private func row(_ title: LocalizedStringKey, _ value: String) -> some View {
        GridRow {
            Text(title)
                .foregroundStyle(.secondary)
                .gridColumnAlignment(.trailing)
            Text(value)
                .textSelection(.enabled)
        }
    }

    private static func format(_ milliseconds: Int) -> String {
        Duration.milliseconds(milliseconds).formatted(
            .units(allowed: [.hours, .minutes, .seconds], width: .abbreviated, maximumUnitCount: 2,
                   fractionalPart: .show(length: 1)))
    }
}
