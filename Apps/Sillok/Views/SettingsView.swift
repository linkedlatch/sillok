import SillokCore
import SillokModels
import SwiftUI

struct SettingsView: View {
    @Environment(AppModel.self) private var model

    private static let languages = [("ko", "한국어"), ("en", "영어"), ("ja", "일본어"), ("zh", "중국어"), ("es", "스페인어"), ("fr", "프랑스어"), ("de", "독일어")]

    var body: some View {
        @Bindable var model = model
        Form {
            Section {
                Picker("전사 모드", selection: $model.settings.whisperID) {
                    Text("속도 우선 (large-v3-turbo)").tag(ModelSelection.defaultWhisperID)
                    Text("정확도 우선 (large-v3)").tag(ModelSelection.accuracyWhisperID)
                }
                Toggle("Neural Engine 가속 (Core ML)", isOn: $model.settings.useCoreML)
                    .disabled(model.isBusy)
                Picker("언어", selection: languageBinding) {
                    Text("자동 감지").tag("auto")
                    Text("한국어 + 영어 혼합").tag("koen")
                    ForEach(Self.languages, id: \.0) { Text($0.1).tag($0.0) }
                }
                VStack(alignment: .leading) {
                    Text("용어 목록 (한 줄에 하나)")
                    TextEditor(text: $model.settings.vocabularyText)
                        .font(.body.monospaced())
                        .frame(minHeight: 80)
                }
            } header: {
                Text("전사")
            } footer: {
                Text("용어 목록은 전사할 때 참고 문맥으로 쓰입니다. 새로 추가하는 파일부터 적용됩니다.")
            }

            Section("가져오기") {
                Toggle("가져올 때 앱 내부로 파일 복사", isOn: $model.settings.copyOnImport)
            }

            Section("모델") {
                ForEach(model.manifest.models) { entry in
                    LabeledContent {
                        if model.modelStore.isInstalled(entry) {
                            Button("삭제", role: .destructive) { model.removeModel(entry) }
                                .disabled(model.isBusy)
                        } else {
                            Text("설치 안 됨").foregroundStyle(.secondary)
                        }
                    } label: {
                        Text(entry.displayName)
                        Text(ByteCountFormatter.string(fromByteCount: entry.size, countStyle: .file))
                    }
                }
                LabeledContent("남은 저장 공간", value: ByteCountFormatter.string(fromByteCount: model.modelStore.availableDiskSpace(), countStyle: .file))
            }

            Section {
                NavigationLink("오픈소스 고지") { LicensesView() }
            }
        }
        .formStyle(.grouped)
        .navigationTitle("설정")
        .onChange(of: model.settings) { model.refreshModelStatus() }
    }

    private var languageBinding: Binding<String> {
        Binding {
            switch model.settings.languageMode {
            case .auto: "auto"
            case .koreanEnglish: "koen"
            case .fixed(let code): code
            }
        } set: { value in
            switch value {
            case "auto": model.settings.languageMode = .auto
            case "koen": model.settings.languageMode = .koreanEnglish
            default: model.settings.languageMode = .fixed(value)
            }
        }
    }
}

struct LicensesView: View {
    private static let notices = [
        ("whisper.cpp", "whisper.cpp"),
        ("OpenAI Whisper (모델 가중치)", "openai-whisper"),
        ("Silero VAD", "silero-vad"),
    ]

    var body: some View {
        List(Self.notices, id: \.1) { notice in
            DisclosureGroup(notice.0) {
                Text(Self.text(notice.1))
                    .font(.caption.monospaced())
                    .textSelection(.enabled)
            }
        }
        .navigationTitle("오픈소스 고지")
    }

    private static func text(_ name: String) -> String {
        guard let url = Bundle.main.url(forResource: name, withExtension: "txt") else { return "" }
        return (try? String(contentsOf: url, encoding: .utf8)) ?? ""
    }
}
