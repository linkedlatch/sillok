import SwiftUI

@main
struct SillokApp: App {
    @NSApplicationDelegateAdaptor private var delegate: AppDelegate
    @State private var models: ModelManager
    @State private var library: Library
    @State private var queue: JobQueue
    @State private var player = Player()
    @FocusedValue(\.find) private var find

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
                .environment(delegate.navigation)
                .task {
                    delegate.queue = queue
                    models.refresh()
                }
        }
        .commands {
            CommandGroup(replacing: .newItem) {
                Button("New Transcription…") { delegate.navigation.importing = true }
                    .keyboardShortcut("n")
                    .disabled(models.state != .ready)
            }
            CommandGroup(after: .textEditing) {
                Button("Find…") { find?() }
                    .keyboardShortcut("f")
                    .disabled(find == nil)
            }
        }

        Settings {
            SettingsView()
                .environment(models)
                .environment(library)
                .environment(queue)
        }
    }
}

@MainActor
final class AppDelegate: NSObject, NSApplicationDelegate {
    let navigation = Navigation()
    var queue: JobQueue?

    func application(_ application: NSApplication, open urls: [URL]) {
        navigation.adding += urls.filter(\.isFileURL)
    }

    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        guard queue?.isAdding == true else { return .terminateNow }
        let alert = NSAlert()
        alert.messageText = String(localized: "Files are still being added.")
        alert.informativeText = String(localized: "Files not added yet are left out if you quit now. You can add them again later.")
        alert.addButton(withTitle: String(localized: "Quit"))
        alert.addButton(withTitle: String(localized: "Cancel"))
        return alert.runModal() == .alertFirstButtonReturn ? .terminateNow : .terminateCancel
    }
}
