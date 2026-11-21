import SillokEngine
import SwiftUI

struct NewJobView: View {
    let sources: [URL]
    let start: (JobOptions, CatalogModel) -> Void

    @Environment(\.dismiss) private var dismiss
    @Environment(ModelManager.self) private var models
    @State private var modelID = ""
    @AppStorage("language") private var language = "auto"
    @AppStorage("diarize") private var diarize = true
    @State private var glossary = ""

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            Form {
                Section {
                    ForEach(sources, id: \.self) { source in
                        Label(source.lastPathComponent, systemImage: "waveform")
                            .lineLimit(1)
                            .truncationMode(.middle)
                    }
                }
                Section {
                    Picker("Model", selection: $modelID) {
                        TranscriptionModelOptions(families: installedFamilies)
                    }
                    Picker("Language", selection: $language) {
                        Text("Detect automatically").tag("auto")
                        Divider()
                        ForEach(sortedLanguages, id: \.self) { code in
                            Text(Self.name(of: code)).tag(code)
                        }
                    }
                    Toggle("Identify speakers", isOn: $diarize)
                }
                Section {
                    TextField("Terms", text: $glossary, prompt: Text("Names and jargon, one per line"), axis: .vertical)
                        .lineLimit(3...6)
                } footer: {
                    Text("Terms the recording may contain help the model spell them.")
                        .foregroundStyle(.secondary)
                }
            }
            .formStyle(.grouped)

            HStack {
                Spacer()
                Button("Cancel", role: .cancel) { dismiss() }
                    .keyboardShortcut(.cancelAction)
                Button("Transcribe") {
                    if let model { start(JobOptions(language: language, diarize: diarize, glossary: terms), model) }
                    dismiss()
                }
                .keyboardShortcut(.defaultAction)
                .disabled(model == nil)
            }
            .padding([.horizontal, .bottom], 20)
        }
        .frame(width: 460)
        .onAppear {
            modelID = models.transcriptionModel?.id ?? ""
        }
        .onChange(of: modelID) {
            if language != "auto", !languages.contains(language) { language = "auto" }
        }
    }

    private var model: CatalogModel? {
        models.transcriptionModels.first { $0.id == modelID }
    }

    private var languages: [String] {
        model?.languages ?? []
    }

    private var installedFamilies: [[CatalogModel]] {
        models.transcriptionFamilies.map { $0.filter(models.isInstalled) }.filter { !$0.isEmpty }
    }

    private var terms: [String] {
        glossary
            .split(whereSeparator: { $0.isNewline || $0 == "," })
            .map { $0.trimmingCharacters(in: .whitespaces) }
            .filter { !$0.isEmpty }
    }

    private var sortedLanguages: [String] {
        languages.sorted { Self.name(of: $0).localizedStandardCompare(Self.name(of: $1)) == .orderedAscending }
    }

    static func name(of code: String) -> String {
        Locale.current.localizedString(forLanguageCode: code) ?? code
    }
}
