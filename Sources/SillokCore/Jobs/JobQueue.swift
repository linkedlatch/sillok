import Foundation

public actor JobQueue {
    public enum Event: Sendable, Equatable {
        case started(UUID)
        case finished(UUID)
        case failed(UUID, String)
        case cancelled(UUID)
    }

    public typealias Work = @Sendable () async throws -> Void

    public nonisolated let events: AsyncStream<Event>
    private let continuation: AsyncStream<Event>.Continuation
    private var pending: [(id: UUID, work: Work)] = []
    private var running: (id: UUID, work: Work, task: Task<Void, Never>)?
    private var rerun: Set<UUID> = []

    public init() {
        (events, continuation) = AsyncStream.makeStream()
    }

    public var pendingIDs: [UUID] { pending.map(\.id) }
    public var runningID: UUID? { running?.id }

    public func enqueue(_ id: UUID, work: @escaping Work) {
        if running?.id == id {
            rerun.insert(id)
            return
        }
        guard !pending.contains(where: { $0.id == id }) else { return }
        pending.append((id, work))
        startNextIfIdle()
    }

    public func cancel(_ id: UUID) {
        if let index = pending.firstIndex(where: { $0.id == id }) {
            pending.remove(at: index)
            continuation.yield(.cancelled(id))
        } else if running?.id == id {
            rerun.remove(id)
            running?.task.cancel()
        }
    }

    private func startNextIfIdle() {
        guard running == nil, !pending.isEmpty else { return }
        let job = pending.removeFirst()
        continuation.yield(.started(job.id))
        running = (job.id, job.work, Task {
            let event: Event
            do {
                try await job.work()
                event = Task.isCancelled ? .cancelled(job.id) : .finished(job.id)
            } catch is CancellationError {
                event = .cancelled(job.id)
            } catch {
                event = .failed(job.id, String(describing: error))
            }
            self.complete(event)
        })
    }

    private func complete(_ event: Event) {
        if let finished = running, rerun.remove(finished.id) != nil {
            pending.insert((finished.id, finished.work), at: 0)
        }
        running = nil
        continuation.yield(event)
        startNextIfIdle()
    }
}
