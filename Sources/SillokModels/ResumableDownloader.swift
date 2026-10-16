import Foundation
import SillokCore

public final class ResumableDownloader: NSObject, URLSessionDataDelegate, @unchecked Sendable {
    public typealias Progress = @Sendable (_ received: Int64, _ total: Int64) -> Void

    private let configuration: URLSessionConfiguration
    private let lock = NSLock()
    private var output: FileHandle?
    private var received: Int64 = 0
    private var total: Int64 = 0
    private var progress: Progress?
    private var continuation: CheckedContinuation<Void, Error>?
    private var statusError: Error?

    public init(configuration: URLSessionConfiguration = .default) {
        self.configuration = configuration
    }

    public func download(_ entry: ModelEntry, partURL: URL, progress: Progress? = nil) async throws {
        var lastError = ModelError.allSourcesFailed(entry.fileName)
        for url in entry.urls {
            do {
                try await fetch(url, entry: entry, partURL: partURL, progress: progress)
                guard try fileSize(partURL) == entry.size else { throw ModelError.sizeMismatch(entry.fileName) }
                guard try FileHasher.sha256(of: partURL) == entry.sha256.lowercased() else {
                    try? FileManager.default.removeItem(at: partURL)
                    throw ModelError.checksumMismatch(entry.fileName)
                }
                return
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

    private func fetch(_ url: URL, entry: ModelEntry, partURL: URL, progress: Progress?) async throws {
        try FileManager.default.createDirectory(at: partURL.deletingLastPathComponent(), withIntermediateDirectories: true)
        if !FileManager.default.fileExists(atPath: partURL.path) {
            FileManager.default.createFile(atPath: partURL.path, contents: nil)
        }
        var existing = try fileSize(partURL)
        if existing > entry.size {
            try Data().write(to: partURL)
            existing = 0
        }
        guard existing < entry.size else { return }

        var request = URLRequest(url: url)
        if existing > 0 { request.setValue("bytes=\(existing)-", forHTTPHeaderField: "Range") }

        let handle = try FileHandle(forWritingTo: partURL)
        try handle.seek(toOffset: UInt64(existing))
        defer { try? handle.close() }

        let session = URLSession(configuration: configuration, delegate: self, delegateQueue: nil)
        defer { session.finishTasksAndInvalidate() }
        let task = session.dataTask(with: request)
        try await withTaskCancellationHandler {
            try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<Void, Error>) in
                lock.withLock {
                    self.output = handle
                    self.received = existing
                    self.total = entry.size
                    self.progress = progress
                    self.continuation = continuation
                    self.statusError = nil
                }
                task.resume()
            }
        } onCancel: {
            task.cancel()
        }
    }

    public func urlSession(_ session: URLSession, dataTask: URLSessionDataTask, didReceive response: URLResponse, completionHandler: @escaping (URLSession.ResponseDisposition) -> Void) {
        let status = (response as? HTTPURLResponse)?.statusCode ?? 0
        lock.withLock {
            switch status {
            case 206:
                completionHandler(.allow)
            case 200:
                try? output?.truncate(atOffset: 0)
                received = 0
                completionHandler(.allow)
            default:
                statusError = URLError(.badServerResponse)
                completionHandler(.cancel)
            }
        }
    }

    public func urlSession(_ session: URLSession, dataTask: URLSessionDataTask, didReceive data: Data) {
        lock.withLock {
            do {
                try output?.write(contentsOf: data)
                received += Int64(data.count)
                progress?(received, total)
            } catch {
                statusError = error
                dataTask.cancel()
            }
        }
    }

    public func urlSession(_ session: URLSession, task: URLSessionTask, didCompleteWithError error: Error?) {
        let (continuation, failure) = lock.withLock { () -> (CheckedContinuation<Void, Error>?, Error?) in
            let pending = self.continuation
            self.continuation = nil
            self.output = nil
            if let statusError { return (pending, statusError) }
            if let urlError = error as? URLError, urlError.code == .cancelled { return (pending, CancellationError()) }
            return (pending, error)
        }
        if let failure {
            continuation?.resume(throwing: failure)
        } else {
            continuation?.resume()
        }
    }

    private func fileSize(_ url: URL) throws -> Int64 {
        (try FileManager.default.attributesOfItem(atPath: url.path)[.size] as? NSNumber)?.int64Value ?? 0
    }
}
