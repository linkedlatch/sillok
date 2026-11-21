import AVFoundation
import Observation

@MainActor
@Observable
final class Player {
    private(set) var recordingID: UUID?
    private(set) var isPlaying = false
    private(set) var time: Double = 0
    private(set) var duration: Double = 0
    @ObservationIgnored private let player = AVPlayer()
    @ObservationIgnored private var started = false

    func load(_ url: URL, recording: UUID, duration: Double) {
        guard recordingID != recording else { return }
        startObserving()
        player.pause()
        player.replaceCurrentItem(with: AVPlayerItem(url: url))
        recordingID = recording
        self.duration = duration
        time = 0
        isPlaying = false
    }

    func unload(_ recording: UUID) {
        guard recordingID == recording else { return }
        player.pause()
        player.replaceCurrentItem(with: nil)
        recordingID = nil
        isPlaying = false
        time = 0
    }

    func toggle() {
        isPlaying ? pause() : play()
    }

    func play() {
        if time >= duration - 0.05 { seek(to: 0) }
        player.play()
        isPlaying = true
    }

    func pause() {
        player.pause()
        isPlaying = false
    }

    func play(fromMilliseconds start: Int) {
        seek(to: Double(start) / 1000)
        play()
    }

    func seek(to seconds: Double) {
        time = seconds
        player.seek(to: CMTime(seconds: seconds, preferredTimescale: 1000), toleranceBefore: .zero, toleranceAfter: .zero)
    }

    private func startObserving() {
        guard !started else { return }
        started = true
        player.addPeriodicTimeObserver(forInterval: CMTime(value: 1, timescale: 10), queue: .main) { [weak self] time in
            MainActor.assumeIsolated {
                guard let self, self.isPlaying else { return }
                self.time = time.seconds
            }
        }
        NotificationCenter.default.addObserver(forName: AVPlayerItem.didPlayToEndTimeNotification, object: nil,
                                               queue: .main) { [weak self] _ in
            MainActor.assumeIsolated {
                guard let self else { return }
                self.isPlaying = false
                self.time = self.duration
            }
        }
    }
}
