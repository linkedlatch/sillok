import SillokEngine
import SwiftUI

struct ContentView: View {
    @Environment(ModelManager.self) private var models

    var body: some View {
        Group {
            if models.state == .ready {
                ContentUnavailableView {
                    Label("Models Are Ready", systemImage: "checkmark.circle")
                } description: {
                    Text("Engine version \(Engine.version)")
                }
            } else {
                ModelSetupView()
            }
        }
        .frame(minWidth: 640, minHeight: 440)
    }
}

#Preview {
    ContentView()
        .environment(ModelManager())
}
