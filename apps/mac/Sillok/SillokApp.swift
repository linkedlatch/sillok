import SwiftUI

@main
struct SillokApp: App {
    @State private var models: ModelManager
    @State private var library: Library
    @State private var queue: JobQueue
    @State private var player = Player()
    @State private var navigation = Navigation()

    init() {
        let models = ModelManager()
        let library = Library()
        _models = State(initialValue: models)
        _library = State(initialValue: library)
        _queue = State(initialValue: JobQueue(models: models, library: library))
    }

    var body: some Scene {
        Window("Sillok", id: "main") {
            ContentView()
                .environment(models)
                .environment(library)
                .environment(queue)
                .environment(player)
                .environment(navigation)
                .task { models.refresh() }
        }
        .commands {
            CommandGroup(replacing: .newItem) {
                Button("New Transcription…") { navigation.importing = true }
                    .keyboardShortcut("n")
                    .disabled(models.state != .ready)
            }
        }

        Settings {
            SettingsView()
                .environment(models)
        }
    }
}
