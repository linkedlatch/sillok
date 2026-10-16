import Foundation
import SillokCore

public final class ModelDownloader: NSObject, URLSessionDownloadDelegate, @unchecked Sendable {
    public typealias Progress = @Sendable (_ received: Int64, _ total: Int64) -> Void

    public static let backgroundIdentifier = "com.linkedlatch.sillok.models"
    public static let shared = ModelDownloader(configuration: .background(withIdentifier: backgroundIdentifier))

    private struct Job {
        var continuation: CheckedContinuation<Void, Error>
        var staging: URL
        var total: Int64
        var progress: Progress?
        var failure: Error?
        var cancelling = false
    }

    private let configuration: URLSessionConfiguration
    private let lock = NSLock()
    private var storedSession: URLSession?
    private var jobs: [Int: Job] = [:]
    private var cancelledBeforeStart: Set<Int> = []
    private var eventsContinuation: CheckedContinuation<Void, Never>?
    private var eventsFinishedEarly = false

    public init(configuration: URLSessionConfiguration) {
        self.configuration = configuration
    }

    private var session: URLSession {
        lock.withLock {
            if let storedSession { return storedSession }
            let session = URLSession(configuration: configuration, delegate: self, delegateQueue: nil)
            storedSession = session
            return session
        }
    }

    static func stagingURL(for entry: ModelEntry, in directory: URL) -> URL {
        directory.appendingPathComponent(entry.fileName + ".download")
    }

    static func resumeDataURL(forStaging staging: URL) -> URL {
        staging.deletingPathExtension().appendingPathExtension("resume")
    }

    public func handleBackgroundEvents() async {
        await withCheckedContinuation { (continuation: CheckedContinuation<Void, Never>) in
            let (previous, finished) = lock.withLock { () -> (CheckedContinuation<Void, Never>?, Bool) in
                let previous = eventsContinuation
                if eventsFinishedEarly {
                    eventsFinishedEarly = false
                    eventsContinuation = nil
                    return (previous, true)
                }
                eventsContinuation = continuation
                return (previous, false)
            }
            previous?.resume()
            if finished {
                continuation.resume()
            } else {
                _ = session
            }
        }
    }

    public func download(_ entry: ModelEntry, into directory: URL, progress: Progress? = nil) async throws -> URL {
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        let staging = Self.stagingURL(for: entry, in: directory)
        let resume = Self.resumeDataURL(forStaging: staging)
        if FileManager.default.fileExists(atPath: staging.path), (try? verify(staging, entry)) != nil {
            try? FileManager.default.removeItem(at: resume)
            return staging
        }

        var resumeData = try? Data(contentsOf: resume)
        try? FileManager.default.removeItem(at: resume)
        for task in await session.allTasks where task.taskDescription == staging.path {
            if let task = task as? URLSessionDownloadTask, let data = await task.cancelByProducingResumeData() {
                resumeData = data
            } else {
                task.cancel()
            }
        }

        var lastError = ModelError.allSourcesFailed(entry.fileName)
        if let resumeData {
            do {
                return try await fetch(session.downloadTask(withResumeData: resumeData), entry: entry, staging: staging, progress: progress)
            } catch is CancellationError {
                throw CancellationError()
            } catch let error as ModelError {
                lastError = error
            } catch {
                if FileManager.default.fileExists(atPath: resume.path) { throw lastError }
                try? resumeData.write(to: resume)
            }
        }
        for url in entry.urls {
            do {
                return try await fetch(session.downloadTask(with: url), entry: entry, staging: staging, progress: progress)
            } catch is CancellationError {
                throw CancellationError()
            } catch let error as ModelError {
                lastError = error
            } catch {
                continue
            }
        }
        throw lastError
    }

    private func fetch(_ task: URLSessionDownloadTask, entry: ModelEntry, staging: URL, progress: Progress?) async throws -> URL {
        try Task.checkCancellation()
        task.taskDescription = staging.path
        let id = task.taskIdentifier
        try await withTaskCancellationHandler {
            try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<Void, Error>) in
                let cancelled = lock.withLock {
                    if cancelledBeforeStart.remove(id) != nil { return true }
                    jobs[id] = Job(continuation: continuation, staging: staging, total: entry.size, progress: progress)
                    return false
                }
                if cancelled {
                    continuation.resume(throwing: CancellationError())
                } else {
                    task.resume()
                }
            }
        } onCancel: {
            cancel(task)
        }
        try? FileManager.default.removeItem(at: Self.resumeDataURL(forStaging: staging))
        try verify(staging, entry)
        return staging
    }

    private func cancel(_ task: URLSessionDownloadTask) {
        let id = task.taskIdentifier
        lock.withLock {
            if jobs[id] != nil {
                jobs[id]?.cancelling = true
            } else {
                cancelledBeforeStart.insert(id)
            }
        }
        task.cancel(byProducingResumeData: { data in
            guard let job = self.lock.withLock({ self.jobs.removeValue(forKey: id) }) else { return }
            if let data { try? data.write(to: Self.resumeDataURL(forStaging: job.staging)) }
            job.continuation.resume(throwing: CancellationError())
        })
    }

    private func verify(_ file: URL, _ entry: ModelEntry) throws {
        let size = (try FileManager.default.attributesOfItem(atPath: file.path)[.size] as? NSNumber)?.int64Value
        guard size == entry.size else {
            try? FileManager.default.removeItem(at: file)
            throw ModelError.sizeMismatch(entry.fileName)
        }
        guard try FileHasher.sha256(of: file) == entry.sha256.lowercased() else {
            try? FileManager.default.removeItem(at: file)
            throw ModelError.checksumMismatch(entry.fileName)
        }
    }

    public func urlSession(_ session: URLSession, downloadTask: URLSessionDownloadTask, didWriteData bytesWritten: Int64, totalBytesWritten: Int64, totalBytesExpectedToWrite: Int64) {
        guard let job = lock.withLock({ jobs[downloadTask.taskIdentifier] }) else { return }
        job.progress?(totalBytesWritten, job.total)
    }

    public func urlSession(_ session: URLSession, downloadTask: URLSessionDownloadTask, didFinishDownloadingTo location: URL) {
        guard let path = downloadTask.taskDescription else { return }
        let staging = URL(fileURLWithPath: path)
        var failure: Error?
        do {
            let status = (downloadTask.response as? HTTPURLResponse)?.statusCode ?? 0
            guard (200..<300).contains(status) else { throw URLError(.badServerResponse) }
            try? FileManager.default.removeItem(at: staging)
            try FileManager.default.moveItem(at: location, to: staging)
        } catch {
            failure = error
        }
        lock.withLock { jobs[downloadTask.taskIdentifier]?.failure = failure }
    }

    public func urlSession(_ session: URLSession, task: URLSessionTask, didCompleteWithError error: Error?) {
        let id = task.taskIdentifier
        let (job, cancelling) = lock.withLock { () -> (Job?, Bool) in
            if jobs[id]?.cancelling == true { return (nil, true) }
            return (jobs.removeValue(forKey: id), false)
        }
        guard !cancelling else { return }
        let cancelled = (error as? URLError)?.code == .cancelled
        if !cancelled, let path = task.taskDescription,
           let data = (error as NSError?)?.userInfo[NSURLSessionDownloadTaskResumeData] as? Data {
            try? data.write(to: Self.resumeDataURL(forStaging: URL(fileURLWithPath: path)))
        }
        guard let job else { return }
        if let failure = job.failure ?? error {
            job.continuation.resume(throwing: cancelled ? CancellationError() : failure)
        } else {
            job.continuation.resume()
        }
    }

    public func urlSessionDidFinishEvents(forBackgroundURLSession session: URLSession) {
        let continuation = lock.withLock { () -> CheckedContinuation<Void, Never>? in
            let pending = eventsContinuation
            eventsContinuation = nil
            if pending == nil { eventsFinishedEarly = true }
            return pending
        }
        continuation?.resume()
    }
}
