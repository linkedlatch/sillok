import SillokEngine
import SwiftUI

struct SettingsView: View {
    var body: some View {
        TabView {
            Tab("Models", systemImage: "square.stack.3d.up") {
                ModelSettingsView()
            }
            Tab("Performance", systemImage: "cpu") {
                PerformanceSettingsView()
            }
        }
        .frame(width: 520)
    }
}

private struct ModelSettingsView: View {
    @Environment(ModelManager.self) private var models

    var body: some View {
        Form {
            ForEach(models.transcriptionFamilies, id: \.[0].family) { family in
                Section {
                    ForEach(family) { model in
                        LabeledContent {
                            control(for: model)
                        } label: {
                            model.recommended
                                ? Text("\(model.quantization) (recommended)") : Text(verbatim: model.quantization)
                            if let failure = models.failure, failure.model == model.id {
                                Text(failure.message)
                                    .foregroundStyle(.red)
                            } else {
                                Text(model.footprint)
                            }
                        }
                    }
                } header: {
                    Text("\(family[0].family) · \(family[0].languages?.count ?? 0) languages")
                } footer: {
                    if family[0].family == models.transcriptionFamilies.last?[0].family {
                        Text("Changes apply from the next transcription.")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                }
            }
        }
        .formStyle(.grouped)
        .fixedSize(horizontal: false, vertical: true)
    }

    @ViewBuilder
    private func control(for model: CatalogModel) -> some View {
        if let download = models.download, download.model == model.id {
            HStack {
                ProgressView(value: download.progress?.fraction ?? 0)
                    .frame(width: 100)
                Button("Cancel") { models.cancel() }
            }
        } else if !models.isInstalled(model) {
            Button("Download \((models.downloadSizes[model.id] ?? 0).formatted(.byteCount(style: .file)))") {
                models.install(model.id)
            }
            .disabled(models.download != nil)
        } else if model.id == models.transcriptionModel?.id {
            Label("In Use", systemImage: "checkmark")
                .foregroundStyle(.secondary)
        } else {
            Button("Use") { models.choose(transcriptionModel: model.id) }
        }
    }
}

private struct PerformanceSettingsView: View {
    @AppStorage(EngineSettings.deviceKey) private var device = EngineConfig.Device.gpu.rawValue
    @AppStorage(EngineSettings.npuKey) private var npu = true

    var body: some View {
        Form {
            Section {
                Picker("Compute on", selection: $device) {
                    Text("GPU").tag(EngineConfig.Device.gpu.rawValue)
                    Text("CPU").tag(EngineConfig.Device.cpu.rawValue)
                }
                Toggle("Run Whisper's encoder on the Neural Engine", isOn: $npu)
            } footer: {
                Text("Changes apply from the next transcription.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
        }
        .formStyle(.grouped)
        .fixedSize(horizontal: false, vertical: true)
    }
}

extension CatalogModel {
    var title: String {
        "\(family) \(quantization)"
    }

    var footprint: String {
        let size = files.map(\.size).reduce(0, +).formatted(.byteCount(style: .file))
        let memory = (memory ?? 0).formatted(.byteCount(style: .memory))
        return String(localized: "\(size) download · \(memory) of memory")
    }

    var details: String {
        String(localized: "\(languages?.count ?? 0) languages · \(footprint)")
    }
}
