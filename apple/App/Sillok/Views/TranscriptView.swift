import SillokCore
import SillokNative
import SwiftUI
import UniformTypeIdentifiers

struct TranscriptView: View {
    @Environment(AppModel.self) private var model
    let projectID: UUID
    @State private var exportFormat: ExportFormat?
    @State private var relinking = false
    @State private var sourceMissing = false

    var body: some View {
        if let project = model.project(projectID) {
            VStack(spacing: 0) {
                if sourceMissing {
                    MissingSourceBanner { relinking = true }
                }
                if model.memoryWarning {
                    Label("메모리가 부족합니다. 다른 앱을 종료하면 전사가 더 안정적입니다", systemImage: "memorychip")
                        .frame(maxWidth: .infinity)
                        .padding(8)
                        .background(.orange.opacity(0.15))
                }
                if let progress = model.progress[projectID] {
                    ProgressView(value: progress.fractionCompleted) {
                        Text(progress.statusText)
                    }
                    .padding()
                }
                let showSpeakers = Set(project.segments.compactMap(\.speaker)).count > 1
                List(project.segments) { segment in
                    SegmentRow(segment: segment, showSpeaker: showSpeakers) { model.play(project, at: segment.start) }
                }
                .overlay {
                    if project.segments.isEmpty, model.progress[projectID] == nil {
                        ContentUnavailableView("전사 결과가 없습니다", systemImage: "text.alignleft")
                    }
                }
            }
            .navigationTitle(project.sourceName)
            .task(id: project.source) { sourceMissing = model.sourceURL(for: project) == nil }
            .toolbar {
                ToolbarItem {
                    Menu("내보내기", systemImage: "square.and.arrow.up") {
                        Button("텍스트 (.txt)") { exportFormat = .txt }
                        Button("자막 (.srt)") { exportFormat = .srt }
                    }
                    .disabled(project.segments.isEmpty)
                }
                if model.playback.isPlaying {
                    ToolbarItem {
                        Button("일시 정지", systemImage: "pause.fill") { model.playback.pause() }
                    }
                }
            }
            .fileExporter(
                isPresented: Binding(get: { exportFormat != nil }, set: { if !$0 { exportFormat = nil } }),
                document: TextFile(text: exportFormat.map { $0.render(project.segments) } ?? ""),
                contentType: exportFormat?.contentType ?? .plainText,
                defaultFilename: (project.sourceName as NSString).deletingPathExtension
            ) { _ in exportFormat = nil }
            .fileImporter(isPresented: $relinking, allowedContentTypes: [.audiovisualContent]) { result in
                if case .success(let url) = result { model.relink(projectID, to: url) }
            }
        }
    }
}

private struct SegmentRow: View {
    let segment: Segment
    let showSpeaker: Bool
    let play: () -> Void

    private static let speakerColors: [Color] = [.blue, .orange, .green, .purple, .pink, .teal, .brown, .indigo]

    var body: some View {
        HStack(alignment: .firstTextBaseline, spacing: 12) {
            Button(TimeFormat.clock(segment.start), action: play)
                .buttonStyle(.borderless)
                .font(.callout.monospacedDigit())
                .help("이 위치부터 재생")
            if showSpeaker, let speaker = segment.speaker {
                Text("화자 \(speaker)")
                    .font(.caption.weight(.semibold))
                    .foregroundStyle(Self.speakerColors[max((Int(speaker) ?? 1) - 1, 0) % Self.speakerColors.count])
                    .fixedSize()
            }
            Text(segment.text)
                .textSelection(.enabled)
                .opacity(segment.isLowConfidence ? 0.45 : 1)
                .frame(maxWidth: .infinity, alignment: .leading)
            if segment.isLowConfidence {
                Image(systemName: "questionmark.circle")
                    .foregroundStyle(.secondary)
                    .help("인식 신뢰도가 낮은 구간입니다. 원본을 들어 확인해 주세요")
            }
            Text(segment.language.uppercased())
                .font(.caption2.monospaced())
                .foregroundStyle(.secondary)
        }
    }
}

private struct MissingSourceBanner: View {
    let relink: () -> Void

    var body: some View {
        HStack {
            Label("원본 파일을 찾을 수 없습니다. 파일 위치를 다시 지정해 주세요", systemImage: "exclamationmark.triangle")
            Spacer()
            Button("파일 재연결", action: relink)
        }
        .padding(10)
        .background(.yellow.opacity(0.2))
    }
}

enum ExportFormat {
    case txt
    case srt

    func render(_ segments: [Segment]) -> String {
        switch self {
        case .txt: (try? NativeExport.render(segments, as: .txt, speakerFormat: UserMessage.speakerFormat)) ?? ""
        case .srt: (try? NativeExport.render(segments, as: .srt, speakerFormat: UserMessage.speakerFormat)) ?? ""
        }
    }

    var contentType: UTType {
        switch self {
        case .txt: .plainText
        case .srt: UTType(filenameExtension: "srt", conformingTo: .plainText) ?? .plainText
        }
    }
}

struct TextFile: FileDocument {
    static var readableContentTypes: [UTType] { [.plainText] }
    static var writableContentTypes: [UTType] {
        [.plainText, UTType(filenameExtension: "srt", conformingTo: .plainText) ?? .plainText]
    }

    var text: String

    init(text: String) {
        self.text = text
    }

    init(configuration: ReadConfiguration) throws {
        text = String(decoding: configuration.file.regularFileContents ?? Data(), as: UTF8.self)
    }

    func fileWrapper(configuration: WriteConfiguration) throws -> FileWrapper {
        FileWrapper(regularFileWithContents: Data(text.utf8))
    }
}
