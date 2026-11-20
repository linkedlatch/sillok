import SwiftUI

@main
struct SillokApp: App {
    @State private var models: ModelManager
    @State private var library: Library
    @State private var queue: JobQueue

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
                .task { models.refresh() }
        }
    }
}
