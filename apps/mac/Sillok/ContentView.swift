import SillokEngine
import SwiftUI

struct ContentView: View {
    var body: some View {
        Text("Engine version \(Engine.version)")
            .padding()
            .frame(minWidth: 480, minHeight: 320)
    }
}

#Preview {
    ContentView()
}
