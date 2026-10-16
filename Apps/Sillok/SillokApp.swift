import SwiftUI

@main
struct SillokApp: App {
    @State private var model = AppModel()

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environment(model)
                .task { model.start() }
        }
        #if os(macOS)
        Settings {
            SettingsView()
                .environment(model)
                .frame(width: 560, height: 620)
        }
        #endif
    }
}
