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
    @Environment(JobQueue.self) private var queue
    @AppStorage(EngineSettings.transcriptionModelKey) private var selection = ""

    var body: some View {
        Form {
            Section {
                Picker("Transcription model", selection: chosen) {
                    ForEach(models.transcriptionModels) { model in
                        VStack(alignment: .leading, spacing: 2) {
                            model.recommended ? Text("\(model.title) (recommended)") : Text(verbatim: model.title)
                            Text(details(of: model))
                                .font(.caption)
                                .foregroundStyle(.secondary)
                        }
                        .tag(model.id)
                    }
                }
                .pickerStyle(.radioGroup)
                .disabled(!queue.items.isEmpty)
            } footer: {
                Text(
                    queue.items.isEmpty
                        ? "Choosing a model whose files are missing asks to download them."
                        : "The model can be changed when no recording is being transcribed."
                )
                .font(.caption)
                .foregroundStyle(.secondary)
            }
        }
        .formStyle(.grouped)
        .fixedSize(horizontal: false, vertical: true)
    }

    private var chosen: Binding<String> {
        Binding(
            get: {
                selection.isEmpty
                    ? models.transcriptionModels.first(where: \.recommended)?.id ?? "" : selection
            },
            set: { id in
                selection = models.transcriptionModels.first { $0.id == id }?.recommended == true ? "" : id
                models.refresh()
            }
        )
    }

    private func details(of model: CatalogModel) -> String {
        let languages = model.languages?.count ?? 0
        let size = model.files.map(\.size).reduce(0, +).formatted(.byteCount(style: .file))
        let memory = (model.memory ?? 0).formatted(.byteCount(style: .memory))
        return String(localized: "\(languages) languages · \(size) download · \(memory) of memory")
    }
}

private struct PerformanceSettingsView: View {
    @AppStorage(EngineSettings.deviceKey) private var device = EngineConfig.Device.gpu.rawValue
    @AppStorage(EngineSettings.npuKey) private var npu = true
    @AppStorage(EngineSettings.threadsKey) private var threads = 0

    var body: some View {
        Form {
            Section {
                Picker("Compute on", selection: $device) {
                    Text("GPU").tag(EngineConfig.Device.gpu.rawValue)
                    Text("CPU").tag(EngineConfig.Device.cpu.rawValue)
                }
                Toggle("Run Whisper's encoder on the Neural Engine", isOn: $npu)
                Picker("CPU threads", selection: $threads) {
                    Text("Automatic").tag(0)
                    ForEach(1...ProcessInfo.processInfo.activeProcessorCount, id: \.self) { count in
                        Text(verbatim: "\(count)").tag(count)
                    }
                }
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
}
