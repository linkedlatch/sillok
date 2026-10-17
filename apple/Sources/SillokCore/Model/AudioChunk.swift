import Foundation

public struct AudioChunk: Codable, Hashable, Sendable {
    public var start: TimeInterval
    public var end: TimeInterval
    public var cutBefore: TimeInterval?
    public var cutAfter: TimeInterval?

    public init(start: TimeInterval, end: TimeInterval, cutBefore: TimeInterval? = nil, cutAfter: TimeInterval? = nil) {
        self.start = start
        self.end = end
        self.cutBefore = cutBefore
        self.cutAfter = cutAfter
    }

    public var duration: TimeInterval { end - start }
}
