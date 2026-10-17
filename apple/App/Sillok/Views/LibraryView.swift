import SillokCore
import SwiftUI

struct LibraryView: View {
    @Environment(AppModel.self) private var model
    @Binding var selection: UUID?

    var body: some View {
        List(selection: $selection) {
            ForEach(model.projects) { project in
                ProjectRow(project: project, progress: model.progress[project.id])
                    .tag(project.id)
                    .contextMenu {
                        if project.status == .transcribing || project.status == .pending {
                            Button("전사 취소") { model.cancel(project.id) }
                        } else if project.status != .completed {
                            Button("다시 시도") { model.retry(project.id) }
                        }
                        Button("삭제", role: .destructive) {
                            if selection == project.id { selection = nil }
                            model.delete(project.id)
                        }
                    }
            }
        }
        .overlay {
            if model.projects.isEmpty {
                ContentUnavailableView("전사한 파일이 없습니다", systemImage: "tray")
            }
        }
    }
}

extension TranscribeProgress {
    var statusText: String {
        switch phase {
        case .analyzing: "음성 구간 분석 중"
        case .transcribing: "전사 중 \(Int(fractionCompleted * 100))%"
        case .diarizing: "화자 구분 중"
        }
    }
}

private struct ProjectRow: View {
    let project: TranscriptProject
    let progress: TranscribeProgress?

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(project.sourceName)
                .lineLimit(1)
            if let progress {
                ProgressView(value: progress.fractionCompleted)
                Text(progress.statusText)
                    .font(.caption)
                    .foregroundStyle(.secondary)
            } else {
                Text(statusText)
                    .font(.caption)
                    .foregroundStyle(statusIsError ? .red : .secondary)
                    .lineLimit(2)
            }
        }
        .padding(.vertical, 2)
    }

    private var statusText: String {
        switch project.status {
        case .pending: "대기 중"
        case .transcribing: "이어서 전사 대기 중"
        case .completed: [project.duration.map(TimeFormat.clock), "\(project.segments.count)개 구간"].compactMap { $0 }.joined(separator: " · ")
        case .cancelled: "취소됨"
        case .failed(let message): message
        }
    }

    private var statusIsError: Bool {
        if case .failed = project.status { return true }
        return false
    }
}
