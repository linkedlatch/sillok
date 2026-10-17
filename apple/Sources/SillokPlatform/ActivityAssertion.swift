import Foundation

public final class ActivityAssertion: @unchecked Sendable {
    private let token: NSObjectProtocol

    public init(reason: String) {
        token = ProcessInfo.processInfo.beginActivity(options: [.userInitiated, .idleSystemSleepDisabled], reason: reason)
    }

    deinit {
        ProcessInfo.processInfo.endActivity(token)
    }
}
