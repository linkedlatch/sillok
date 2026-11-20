import SillokEngine
import SillokStore
import SwiftUI

struct ModelSetupView: View {
    @Environment(ModelManager.self) private var manager

    var body: some View {
        VStack(alignment: .leading, spacing: 20) {
            VStack(alignment: .leading, spacing: 6) {
                Text("Download the Models")
                    .font(.title2.bold())
                Text("Sillok transcribes on this Mac. It needs these models once and works offline afterwards.")
                    .foregroundStyle(.secondary)
            }

            Form {
                ForEach(manager.models) { model in
                    LabeledContent(model.role.title) {
                        Text(verbatim: "\(model.family) \(model.quantization)")
                        Text(model.files.map(\.size).reduce(0, +).formatted(.byteCount(style: .file)))
                            .foregroundStyle(.secondary)
                            .monospacedDigit()
                    }
                }
            }
            .formStyle(.grouped)
            .scrollDisabled(true)
            .fixedSize(horizontal: false, vertical: true)

            status
        }
        .padding(24)
        .frame(maxWidth: 560)
    }

    @ViewBuilder
    private var status: some View {
        switch manager.state {
        case .checking, .ready:
            ProgressView()
                .controlSize(.small)
        case .missing:
            HStack {
                Text("Download size: \(manager.downloadSize.formatted(.byteCount(style: .file)))")
                    .foregroundStyle(.secondary)
                Spacer()
                Button("Download") { manager.install() }
                    .keyboardShortcut(.defaultAction)
            }
        case .installing(let progress):
            HStack(alignment: .bottom, spacing: 12) {
                ProgressView(value: progress?.fraction ?? 0) {
                    progress?.title ?? Text("Starting…")
                } currentValueLabel: {
                    if let progress {
                        Text("\(progress.completedBytes.formatted(.byteCount(style: .file))) of \(progress.totalBytes.formatted(.byteCount(style: .file)))")
                            .monospacedDigit()
                    }
                }
                Button("Cancel") { manager.cancel() }
            }
        case .failed(let message):
            HStack {
                Label(message, systemImage: "exclamationmark.triangle.fill")
                    .foregroundStyle(.red)
                Spacer()
                Button("Try Again") { manager.install() }
                    .keyboardShortcut(.defaultAction)
            }
        }
    }
}

extension ModelRole {
    var title: LocalizedStringKey {
        switch self {
        case .voiceDetection: "Voice detection"
        case .transcription: "Transcription"
        case .diarization: "Speaker diarization"
        }
    }
}

extension InstallProgress {
    var title: Text {
        switch phase {
        case .downloading: Text("Downloading \(file)")
        case .verifying: Text("Checking \(file)")
        case .extracting: Text("Extracting \(file)")
        }
    }
}
