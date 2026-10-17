import SwiftUI
import UniformTypeIdentifiers
#if os(macOS)
import AppKit
#endif

struct ContentView: View {
    @Environment(AppModel.self) private var model
    @Environment(\.scenePhase) private var scenePhase
    @State private var selection: UUID?
    @State private var importing = false
    @State private var showingSettings = false

    var body: some View {
        @Bindable var model = model
        NavigationSplitView {
            LibraryView(selection: $selection)
                .navigationTitle("Sillok")
                .toolbar {
                    ToolbarItem {
                        Button("파일 추가", systemImage: "plus") { importing = true }
                            .disabled(model.modelStatus != .ready)
                    }
                    #if os(iOS)
                    ToolbarItem {
                        Button("설정", systemImage: "gear") { showingSettings = true }
                    }
                    #endif
                }
        } detail: {
            if model.modelStatus != .ready {
                ModelSetupView()
            } else if let selection, model.project(selection) != nil {
                TranscriptView(projectID: selection)
            } else {
                ContentUnavailableView("파일을 끌어다 놓거나 추가하세요", systemImage: "waveform",
                                       description: Text("mp4, mov, m4a, wav, mp3, caf 파일을 기기 안에서 전사합니다"))
            }
        }
        .dropDestination(for: URL.self) { urls, _ in
            guard model.modelStatus == .ready else { return false }
            model.addFiles(urls, fromDrop: true)
            return true
        }
        .fileImporter(isPresented: $importing, allowedContentTypes: [.audiovisualContent], allowsMultipleSelection: true) { result in
            if case .success(let urls) = result { model.addFiles(urls) }
        }
        .alert("알림", isPresented: Binding(get: { model.alert != nil }, set: { if !$0 { model.alert = nil } })) {
            Button("확인") { model.alert = nil }
        } message: {
            Text(model.alert ?? "")
        }
        .onChange(of: scenePhase) {
            if scenePhase == .background { model.playback.pause() }
        }
        #if os(macOS)
        .onReceive(NotificationCenter.default.publisher(for: NSWindow.didMiniaturizeNotification)) { _ in
            model.playback.pause()
        }
        .onReceive(NotificationCenter.default.publisher(for: NSApplication.didHideNotification)) { _ in
            model.playback.pause()
        }
        #endif
        .sheet(isPresented: $showingSettings) {
            NavigationStack {
                SettingsView()
                    .toolbar { Button("닫기") { showingSettings = false } }
            }
        }
    }
}
