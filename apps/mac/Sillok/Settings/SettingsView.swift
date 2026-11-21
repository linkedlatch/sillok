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
    @AppStorage(EngineSettings.npuKey) private var npu = true
    @State private var deleting: CatalogModel?
    @State private var deletingEncoder: CatalogFile?

    var body: some View {
        Form {
            Section {
                Toggle("Use the Neural Engine", isOn: $npu)
            } footer: {
                Text("Whisper runs its encoder on the Neural Engine, which needs an extra download for each Whisper model family. Downloads include it while this is on.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            ForEach(models.transcriptionFamilies, id: \.[0].family) { family in
                Section {
                    ForEach(family) { model in
                        LabeledContent {
                            control(for: model)
                        } label: {
                            model.recommended
                                ? Text("\(model.quantization) (recommended)") : Text(verbatim: model.quantization)
                            if let failure = models.failure, failure.key == model.id {
                                Text(failure.message)
                                    .foregroundStyle(.red)
                            } else {
                                Text(model.footprint)
                            }
                        }
                    }
                    if let encoder = models.encoder(of: family[0]), showsEncoder(encoder, of: family) {
                        LabeledContent {
                            control(for: encoder, of: family)
                        } label: {
                            Text("Neural Engine encoder")
                            if let failure = models.failure, failure.key == encoder.file {
                                Text(failure.message)
                                    .foregroundStyle(.red)
                            } else {
                                Text(encoder.size.formatted(.byteCount(style: .file)))
                            }
                        }
                    }
                } header: {
                    Text("\(family[0].family) · \(family[0].languages?.count ?? 0) languages")
                }
            }
            Section {
                LabeledContent {
                    if models.check == .running {
                        ProgressView().controlSize(.small)
                    } else {
                        Button("Check") { models.checkForDamage() }
                            .disabled(models.download != nil || queue.isWorking)
                    }
                } label: {
                    Text("Check for damage")
                    checkResult
                }
            } footer: {
                Text("Changes apply from the next transcription.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
        }
        .formStyle(.grouped)
        .fixedSize(horizontal: false, vertical: true)
        .onChange(of: npu) { models.refresh() }
        .confirmationDialog(
            Text("Delete \(deleting?.title ?? "")?"),
            isPresented: Binding(get: { deleting != nil }, set: { if !$0 { deleting = nil } }),
            presenting: deleting
        ) { model in
            Button("Delete", role: .destructive) { models.remove(model.id) }
        } message: { _ in
            Text("You can download it again here.")
        }
        .confirmationDialog(
            Text("Delete the Neural Engine encoder?"),
            isPresented: Binding(get: { deletingEncoder != nil }, set: { if !$0 { deletingEncoder = nil } }),
            presenting: deletingEncoder
        ) { encoder in
            Button("Delete", role: .destructive) { models.remove(encoder) }
        } message: { _ in
            Text("Whisper then runs its encoder on the GPU or CPU. You can download it again here.")
        }
    }

    @ViewBuilder
    private var checkResult: some View {
        switch models.check {
        case .running:
            Text("Reading each downloaded file through…")
        case .intact:
            Text("The downloaded models are intact.")
        case .repaired(let count):
            Text("Removed \(count) damaged files. Download them again.")
                .foregroundStyle(.orange)
        case .failed(let message):
            Text(message)
                .foregroundStyle(.red)
        case nil:
            Text("Reads each downloaded file and compares it with the original. Large files take a while.")
        }
    }

    private func showsEncoder(_ encoder: CatalogFile, of family: [CatalogModel]) -> Bool {
        models.isInstalled(encoder) || (npu && family.contains(where: models.isInstalled))
    }

    @ViewBuilder
    private func control(for model: CatalogModel) -> some View {
        if let download = models.download, download.key == model.id {
            progress(download)
        } else if !models.isInstalled(model) {
            Button("Download \((models.downloadSizes[model.id] ?? 0).formatted(.byteCount(style: .file)))") {
                models.install(model.id)
            }
            .disabled(models.download != nil || models.check == .running)
        } else if model.id == models.transcriptionModel?.id {
            Label("In Use", systemImage: "checkmark")
                .foregroundStyle(.secondary)
        } else {
            HStack {
                Button("Use") { models.choose(transcriptionModel: model.id) }
                Button("Delete", systemImage: "trash") { deleting = model }
                    .labelStyle(.iconOnly)
                    .buttonStyle(.borderless)
                    .help("Delete")
                    .disabled(models.download != nil || models.check == .running || queue.uses(model.id))
            }
        }
    }

    @ViewBuilder
    private func control(for encoder: CatalogFile, of family: [CatalogModel]) -> some View {
        if let download = models.download, download.key == encoder.file {
            progress(download)
        } else if !models.isInstalled(encoder) {
            Button("Download") { models.install(encoder) }
                .disabled(models.download != nil || models.check == .running)
        } else {
            Button("Delete", systemImage: "trash") { deletingEncoder = encoder }
                .labelStyle(.iconOnly)
                .buttonStyle(.borderless)
                .help("Delete")
                .disabled(models.download != nil || models.check == .running || family.contains { queue.uses($0.id) })
        }
    }

    private func progress(_ download: ModelManager.Download) -> some View {
        HStack {
            ProgressView(value: download.progress?.fraction ?? 0)
                .frame(width: 100)
            Button("Cancel") { models.cancel() }
        }
    }
}

private struct PerformanceSettingsView: View {
    @AppStorage(EngineSettings.deviceKey) private var device = EngineConfig.Device.gpu.rawValue

    var body: some View {
        Form {
            Section {
                Picker("Compute on", selection: $device) {
                    Text("GPU").tag(EngineConfig.Device.gpu.rawValue)
                    Text("CPU").tag(EngineConfig.Device.cpu.rawValue)
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

    var footprint: String {
        let size = files.filter { !$0.zip }.map(\.size).reduce(0, +).formatted(.byteCount(style: .file))
        let memory = (memory ?? 0).formatted(.byteCount(style: .memory))
        return String(localized: "\(size) download · \(memory) of memory")
    }

    var details: String {
        String(localized: "\(languages?.count ?? 0) languages · \(footprint)")
    }
}
