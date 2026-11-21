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
                    if model.role == .transcription {
                        Picker(selection: chosen) {
                            ForEach(manager.transcriptionFamilies, id: \.[0].family) { family in
                                Section(family[0].family) {
                                    ForEach(family) { option in
                                        option.recommended
                                            ? Text("\(option.title) (recommended)").tag(option.id)
                                            : Text(verbatim: option.title).tag(option.id)
                                    }
                                }
                            }
                        } label: {
                            Text(model.role.title)
                            Text(model.details)
                        }
                        .disabled(manager.download != nil)
                    } else {
                        LabeledContent(model.role.title) {
                            Text(verbatim: model.title)
                            Text(model.files.map(\.size).reduce(0, +).formatted(.byteCount(style: .file)))
                                .foregroundStyle(.secondary)
                                .monospacedDigit()
                        }
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

    private var chosen: Binding<String> {
        Binding(
            get: { manager.transcriptionModel?.id ?? "" },
            set: { manager.choose(transcriptionModel: $0) }
        )
    }

    @ViewBuilder
    private var status: some View {
        switch manager.state {
        case .checking, .ready:
            ProgressView()
                .controlSize(.small)
        case .failed(let message):
            problem(message) { manager.refresh() }
        case .missing:
            if let download = manager.download {
                HStack(alignment: .bottom, spacing: 12) {
                    ProgressView(value: download.progress?.fraction ?? 0) {
                        download.progress?.title ?? Text("Starting…")
                    } currentValueLabel: {
                        if let progress = download.progress {
                            Text("\(progress.completedBytes.formatted(.byteCount(style: .file))) of \(progress.totalBytes.formatted(.byteCount(style: .file)))")
                                .monospacedDigit()
                        }
                    }
                    Button("Cancel") { manager.cancel() }
                }
            } else if let id = manager.transcriptionModel?.id {
                if let failure = manager.failure, failure.model == id {
                    problem(failure.message) { manager.install(id) }
                } else {
                    HStack {
                        Text("Download size: \(manager.downloadSize.formatted(.byteCount(style: .file)))")
                            .foregroundStyle(.secondary)
                        Spacer()
                        Button("Download") { manager.install(id) }
                            .keyboardShortcut(.defaultAction)
                    }
                }
            }
        }
    }

    private func problem(_ message: String, retry: @escaping () -> Void) -> some View {
        HStack {
            Label(message, systemImage: "exclamationmark.triangle.fill")
                .foregroundStyle(.red)
            Spacer()
            Button("Try Again", action: retry)
                .keyboardShortcut(.defaultAction)
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
