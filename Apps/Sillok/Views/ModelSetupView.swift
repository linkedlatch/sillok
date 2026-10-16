import SillokCore
import SillokModels
import SwiftUI
import UniformTypeIdentifiers

struct ModelSetupView: View {
    @Environment(AppModel.self) private var model
    @State private var importing = false

    var body: some View {
        @Bindable var model = model
        Form {
            Section {
                Text("음성은 기기 밖으로 나가지 않습니다. 처음 한 번 전사 모델을 내려받아야 합니다.")
                    .foregroundStyle(.secondary)
                Picker("전사 모드", selection: $model.settings.whisperID) {
                    Text("속도 우선 (large-v3-turbo)").tag(ModelSelection.defaultWhisperID)
                    Text("정확도 우선 (large-v3)").tag(ModelSelection.accuracyWhisperID)
                }
                .disabled(isBusy)
            }

            Section("필요한 파일") {
                ForEach(model.requiredModels) { entry in
                    LabeledContent(entry.displayName) {
                        if model.modelStore.isInstalled(entry) {
                            Label("설치됨", systemImage: "checkmark.circle.fill").foregroundStyle(.green)
                        } else {
                            Text(ByteCountFormatter.string(fromByteCount: entry.size, countStyle: .file))
                        }
                    }
                }
                LabeledContent("내려받을 크기", value: ByteCountFormatter.string(fromByteCount: model.missingModels.reduce(0) { $0 + $1.size }, countStyle: .file))
                LabeledContent("남은 저장 공간", value: ByteCountFormatter.string(fromByteCount: model.modelStore.availableDiskSpace(), countStyle: .file))
            }

            Section {
                status
                HStack {
                    if case .downloading = model.modelStatus {
                        Button("취소") { model.cancelDownload() }
                    } else {
                        Button("내려받기") { model.downloadModels() }
                            .buttonStyle(.borderedProminent)
                            .disabled(isBusy || model.missingModels.isEmpty)
                    }
                    Button("직접 가져오기…") { importing = true }
                        .disabled(isBusy)
                }
            } footer: {
                Text("네트워크를 쓸 수 없으면 Hugging Face(ggerganov/whisper.cpp)에서 받은 .bin 또는 .mlmodelc.zip 파일을 가져올 수 있습니다. 파일은 SHA256으로 검증합니다.")
            }
        }
        .formStyle(.grouped)
        .navigationTitle("모델 준비")
        .onChange(of: model.settings.whisperID) { model.refreshModelStatus() }
        .fileImporter(isPresented: $importing, allowedContentTypes: [.data], allowsMultipleSelection: true) { result in
            if case .success(let urls) = result { urls.forEach(model.importModel) }
        }
    }

    private var isBusy: Bool {
        switch model.modelStatus {
        case .downloading, .installing, .warmingUp: true
        default: false
        }
    }

    @ViewBuilder
    private var status: some View {
        switch model.modelStatus {
        case .downloading(let received, let total):
            ProgressView(value: Double(received), total: Double(max(total, 1))) {
                Text("내려받는 중 \(ByteCountFormatter.string(fromByteCount: received, countStyle: .file)) / \(ByteCountFormatter.string(fromByteCount: total, countStyle: .file))")
            }
        case .installing:
            ProgressView("설치 중")
        case .warmingUp:
            ProgressView("모델 최적화 중… 처음 한 번만 수십 초가 걸립니다")
        case .failed(let message):
            Label(message, systemImage: "exclamationmark.triangle").foregroundStyle(.red)
        case .missing, .ready:
            EmptyView()
        }
    }
}
