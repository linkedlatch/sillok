import os

final class AbortFlag: Sendable {
    private let state = OSAllocatedUnfairLock(initialState: false)

    var isSet: Bool { state.withLock { $0 } }

    func set() { state.withLock { $0 = true } }
}
