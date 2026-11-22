import SillokEngine
import SillokStore
import SwiftUI

struct ModelSetupView: View {
    @Environment(ModelManager.self) private var manager
    @AppStorage(EngineSettings.npuKey) private var npu = true

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
                            TranscriptionModelOptions(families: manager.transcriptionFamilies)
                        } label: {
                            Text(model.role.title)
                            Text(model.details(memory: manager.memory(of: model)))
                        }
                        .disabled(manager.download != nil)
                        if let encoder = manager.encoder(of: model) {
                            Toggle(isOn: $npu) {
                                Text("Use the Neural Engine")
                                Text("Whisper's encoder runs about twice as fast there. It takes another \(manager.downloadSize(of: encoder).formatted(.byteCount(style: .file))) to download and about as much more memory.")
                            }
                            .disabled(manager.download != nil)
                        }
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
            .onChange(of: npu) { manager.refresh() }
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
                if let failure = manager.failure, failure.key == id {
                    problem(failure.message) { manager.install(id) }
                } else {
                    HStack {
                        Group {
                            if let model = manager.transcriptionModel, let encoder = manager.encoderDownload(for: model) {
                                Text("Download size: \(manager.downloadSize.formatted(.byteCount(style: .file))), plus \(manager.downloadSize(of: encoder).formatted(.byteCount(style: .file))) for the Neural Engine encoder")
                            } else {
                                Text("Download size: \(manager.downloadSize.formatted(.byteCount(style: .file)))")
                            }
                        }
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

struct TranscriptionModelOptions: View {
    let families: [[CatalogModel]]

    var body: some View {
        ForEach(families, id: \.[0].family) { family in
            Section(family[0].family) {
                ForEach(family) { option in
                    option.recommended
                        ? Text("\(option.title) (recommended)").tag(option.id)
                        : Text(verbatim: option.title).tag(option.id)
                }
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
