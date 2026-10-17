import Foundation
import Testing
@testable import SillokCore

private actor Log {
    var entries: [String] = []
    func add(_ entry: String) { entries.append(entry) }
}

private func events(_ queue: JobQueue, count: Int) async -> [JobQueue.Event] {
    var collected: [JobQueue.Event] = []
    for await event in queue.events {
        collected.append(event)
        if collected.count == count { break }
    }
    return collected
}

@Test func jobsRunOneAtATimeInOrder() async {
    let queue = JobQueue()
    let log = Log()
    let a = UUID(), b = UUID()
    await queue.enqueue(a) {
        await log.add("a start")
        try await Task.sleep(for: .milliseconds(50))
        await log.add("a end")
    }
    await queue.enqueue(b) { await log.add("b") }

    let received = await events(queue, count: 4)
    #expect(received == [.started(a), .finished(a), .started(b), .finished(b)])
    #expect(await log.entries == ["a start", "a end", "b"])
}

@Test func cancellingPendingAndRunningJobs() async {
    let queue = JobQueue()
    let a = UUID(), b = UUID(), c = UUID()
    await queue.enqueue(a) { try await Task.sleep(for: .seconds(10)) }
    await queue.enqueue(b) {}
    await queue.enqueue(c) { throw CocoaError(.fileNoSuchFile) }
    await queue.cancel(b)
    await queue.cancel(a)

    let received = await events(queue, count: 5)
    #expect(received.first == .started(a))
    #expect(received.contains(.cancelled(b)))
    #expect(received.contains(.cancelled(a)))
    #expect(received.contains(.started(c)))
    #expect(received.contains { if case .failed(c, _) = $0 { true } else { false } })
}

@Test func duplicateEnqueueIsIgnoredAndRunningJobReruns() async {
    let queue = JobQueue()
    let log = Log()
    let a = UUID()
    await queue.enqueue(a) {
        await log.add("run")
        try await Task.sleep(for: .milliseconds(50))
    }
    await queue.enqueue(a) { await log.add("duplicate") }

    let received = await events(queue, count: 4)
    #expect(received == [.started(a), .finished(a), .started(a), .finished(a)])
    #expect(await log.entries == ["run", "run"])
}
