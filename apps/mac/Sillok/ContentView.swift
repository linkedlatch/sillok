import SwiftUI

struct ContentView: View {
    @Environment(ModelManager.self) private var models

    var body: some View {
        Group {
            if models.state == .ready {
                MainView()
            } else {
                ModelSetupView()
            }
        }
        .frame(minWidth: 760, minHeight: 480)
    }
}
