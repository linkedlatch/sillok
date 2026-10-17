import AVFoundation
import Observation

@MainActor
@Observable
public final class PlaybackController {
    public private(set) var currentURL: URL?
    public private(set) var isPlaying = false
    private var player: AVPlayer?
    private var accessing = false
    private var endObserver: NSObjectProtocol?

    public init() {}

    public func play(_ url: URL, at time: TimeInterval) {
        if currentURL != url {
            stop()
            accessing = url.startAccessingSecurityScopedResource()
            let item = AVPlayerItem(url: url)
            player = AVPlayer(playerItem: item)
            currentURL = url
            endObserver = NotificationCenter.default.addObserver(forName: AVPlayerItem.didPlayToEndTimeNotification, object: item, queue: .main) { [weak self] _ in
                MainActor.assumeIsolated { self?.isPlaying = false }
            }
        }
        player?.seek(to: CMTime(seconds: time, preferredTimescale: 600), toleranceBefore: .zero, toleranceAfter: .zero)
        player?.play()
        isPlaying = true
    }

    public func pause() {
        player?.pause()
        isPlaying = false
    }

    public func stop() {
        if let endObserver { NotificationCenter.default.removeObserver(endObserver) }
        endObserver = nil
        player?.pause()
        player = nil
        if accessing, let currentURL { currentURL.stopAccessingSecurityScopedResource() }
        accessing = false
        currentURL = nil
        isPlaying = false
    }
}
