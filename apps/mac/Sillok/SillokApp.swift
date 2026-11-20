import SwiftUI

@main
struct SillokApp: App {
    @State private var models = ModelManager()

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environment(models)
                .task { models.refresh() }
        }
    }
}
