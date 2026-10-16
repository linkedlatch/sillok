import SwiftUI
import SillokWhisper

struct ContentView: View {
    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("Sillok")
                .font(.largeTitle)
            Text("whisper.cpp \(WhisperBuildInfo.version)")
            Text(WhisperBuildInfo.systemInfo)
                .font(.caption.monospaced())
                .textSelection(.enabled)
        }
        .padding()
        .frame(minWidth: 480, minHeight: 240, alignment: .topLeading)
    }
}
