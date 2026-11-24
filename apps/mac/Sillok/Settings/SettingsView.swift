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
            Tab("Storage", systemImage: "internaldrive") {
                StorageSettingsView()
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
                Text("Whisper's encoder runs about twice as fast on the Neural Engine. While this is on, downloads include it and the memory shown counts it.")
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
                            } else if let download = models.download, download.key == model.id {
                                download.status
                            } else {
                                Text(model.footprint(memory: models.memory(of: model)))
                                if !models.isInstalled(model), let encoder = models.encoderDownload(for: model) {
                                    Text("Also downloads the Neural Engine encoder (\(models.downloadSize(of: encoder).formatted(.byteCount(style: .file)))).")
                                }
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
                            } else if let download = models.download, download.key == encoder.file {
                                download.status
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
            progress(download) { deleting = model }
        } else if !models.isInstalled(model) {
            HStack {
                downloadButton(key: model.id, size: models.downloadSizes[model.id] ?? 0) { models.install(model.id) }
                if models.partlyDownloaded.contains(model.id) {
                    deleteButton(key: model.id) { deleting = model }
                }
            }
        } else if model.id == models.transcriptionModel?.id {
            Label("In Use", systemImage: "checkmark")
                .foregroundStyle(.secondary)
        } else {
            HStack {
                Button("Use") { models.choose(transcriptionModel: model.id) }
                deleteButton(key: model.id) { deleting = model }
                    .disabled(queue.uses(model.id))
            }
        }
    }

    @ViewBuilder
    private func control(for encoder: CatalogFile, of family: [CatalogModel]) -> some View {
        if let download = models.download, download.key == encoder.file {
            progress(download) { deletingEncoder = encoder }
        } else if !models.isInstalled(encoder) {
            HStack {
                downloadButton(key: encoder.file, size: models.downloadSize(of: encoder)) { models.install(encoder) }
                if models.partlyDownloaded.contains(encoder.file) {
                    deleteButton(key: encoder.file) { deletingEncoder = encoder }
                }
            }
        } else {
            deleteButton(key: encoder.file) { deletingEncoder = encoder }
                .disabled(family.contains { queue.uses($0.id) })
        }
    }

    private func deleteButton(key: String, action: @escaping () -> Void) -> some View {
        Button("Delete", systemImage: "trash", action: action)
            .labelStyle(.iconOnly)
            .buttonStyle(.borderless)
            .help("Delete")
            .disabled((models.download != nil && models.download?.key != key) || models.check == .running)
    }

    private func downloadButton(key: String, size: Int64, action: @escaping () -> Void) -> some View {
        let size = size.formatted(.byteCount(style: .file))
        return Button(models.partlyDownloaded.contains(key) ? "Resume \(size)" : "Download \(size)", action: action)
            .disabled(models.download != nil || models.check == .running)
    }

    private func progress(_ download: ModelManager.Download, delete: @escaping () -> Void) -> some View {
        HStack {
            ProgressView(value: download.progress?.fraction ?? 0)
                .frame(width: 100)
            Button("Pause") { models.pause() }
            deleteButton(key: download.key, action: delete)
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

private struct StorageSettingsView: View {
    @Environment(ModelManager.self) private var models
    @Environment(Library.self) private var library
    @State private var sizes: [URL: Int64] = [:]

    var body: some View {
        Form {
            Section {
                row("Models", at: models.directory)
                row("Recordings", at: library.store.root)
            } footer: {
                Text("Each recording keeps its copy for listening and its transcript.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
        }
        .formStyle(.grouped)
        .fixedSize(horizontal: false, vertical: true)
        .task {
            let folders = [models.directory, library.store.root]
            sizes = await Task.detached { Dictionary(uniqueKeysWithValues: folders.map { ($0, Self.size(of: $0)) }) }.value
        }
    }

    private func row(_ title: LocalizedStringKey, at folder: URL) -> some View {
        LabeledContent {
            Button("Show in Finder") { NSWorkspace.shared.activateFileViewerSelecting([folder]) }
                .disabled(!FileManager.default.fileExists(atPath: folder.path(percentEncoded: false)))
        } label: {
            Text(title)
            if let size = sizes[folder] {
                Text(size.formatted(.byteCount(style: .file)))
            } else {
                Text("Measuring…")
            }
        }
    }

    private nonisolated static func size(of folder: URL) -> Int64 {
        let keys: [URLResourceKey] = [.isRegularFileKey, .totalFileAllocatedSizeKey]
        guard let files = FileManager.default.enumerator(at: folder, includingPropertiesForKeys: keys) else { return 0 }
        var total: Int64 = 0
        for case let file as URL in files {
            guard let values = try? file.resourceValues(forKeys: Set(keys)), values.isRegularFile == true else { continue }
            total += Int64(values.totalFileAllocatedSize ?? 0)
        }
        return total
    }
}

extension CatalogModel {
    var title: String {
        "\(family) \(quantization)"
    }

    func footprint(memory: Int64) -> String {
        let size = files.filter { !$0.zip }.map(\.size).reduce(0, +).formatted(.byteCount(style: .file))
        return String(localized: "\(size) download · \(memory.formatted(.byteCount(style: .memory))) of memory")
    }

    func details(memory: Int64) -> String {
        String(localized: "\(languages?.count ?? 0) languages · \(footprint(memory: memory))")
    }
}
