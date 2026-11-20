import CryptoKit
import Foundation
import SillokEngine
import Synchronization

public struct PendingFile: Sendable, Hashable {
    public let file: CatalogFile
    public let needsDownload: Bool
}

public struct InstallProgress: Sendable, Equatable {
    public enum Phase: Sendable, Equatable {
        case downloading
        case verifying
        case extracting
    }

    public var file: String
    public var phase: Phase
    public var completedBytes: Int64
    public var totalBytes: Int64

    public var fraction: Double {
        totalBytes > 0 ? Double(completedBytes) / Double(totalBytes) : 0
    }
}

public enum ModelInstallError: Error, Equatable {
    case wrongSize(file: String)
    case wrongHash(file: String)
    case badResponse(file: String, status: Int)
    case extractionFailed(file: String)
}

public enum ModelInstaller {
    public static func pendingFiles(for models: [CatalogModel], in directory: URL) throws -> [PendingFile] {
        let statuses = try Catalog.check(directory: directory)
        let ids = Set(models.map(\.id))
        var seen = Set<String>()
        var pending: [PendingFile] = []
        for model in models {
            for file in model.files where !seen.contains(file.file) {
                seen.insert(file.file)
                let status = statuses.first { ids.contains($0.model) && $0.file == file.file }?.status
                switch status {
                case .ok:
                    continue
                case .extract:
                    pending.append(PendingFile(file: file, needsDownload: false))
                default:
                    pending.append(PendingFile(file: file, needsDownload: true))
                }
            }
        }
        return pending
    }

    public static func install(
        _ pending: [PendingFile],
        into directory: URL,
        session: URLSession = .shared,
        progress: @escaping @Sendable (InstallProgress) -> Void
    ) async throws {
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        let totalBytes = pending.filter(\.needsDownload).map(\.file.size).reduce(0, +)
        var doneBytes: Int64 = 0
        for item in pending {
            let file = item.file
            let destination = directory.appending(path: file.file)
            if item.needsDownload {
                let partial = directory.appending(path: file.file + ".partial")
                try FileManager.default.createDirectory(
                    at: partial.deletingLastPathComponent(), withIntermediateDirectories: true)
                let base = doneBytes
                try await download(file, to: partial, session: session) { bytes in
                    progress(InstallProgress(file: file.file, phase: .downloading,
                                             completedBytes: base + bytes, totalBytes: totalBytes))
                }
                doneBytes += file.size
                progress(InstallProgress(file: file.file, phase: .verifying,
                                         completedBytes: doneBytes, totalBytes: totalBytes))
                do {
                    try verify(partial, against: file)
                } catch let error as ModelInstallError {
                    try? FileManager.default.removeItem(at: partial)
                    throw error
                }
                try? FileManager.default.removeItem(at: destination)
                try FileManager.default.moveItem(at: partial, to: destination)
            }
            try Task.checkCancellation()
            if file.zip {
                progress(InstallProgress(file: file.file, phase: .extracting,
                                         completedBytes: doneBytes, totalBytes: totalBytes))
                try await extract(destination, file: file.file)
            }
        }
    }

    static func download(
        _ file: CatalogFile,
        to partial: URL,
        session: URLSession,
        progress: @escaping @Sendable (Int64) -> Void
    ) async throws {
        if !FileManager.default.fileExists(atPath: partial.path(percentEncoded: false)) {
            FileManager.default.createFile(atPath: partial.path(percentEncoded: false), contents: nil)
        }
        let handle = try FileHandle(forWritingTo: partial)
        defer { try? handle.close() }
        var offset = Int64(try handle.seekToEnd())
        if offset > file.size {
            try handle.truncate(atOffset: 0)
            offset = 0
        }
        guard offset < file.size else { return }

        var request = URLRequest(url: file.url)
        if offset > 0 {
            request.setValue("bytes=\(offset)-", forHTTPHeaderField: "Range")
        }
        let writer = PartialWriter(file: file.file, handle: handle, offset: offset,
                                   step: max(file.size / 200, 1 << 20), report: progress)
        try await writer.run(request, session: session)
    }

    static func verify(_ url: URL, against file: CatalogFile) throws {
        let size = try url.resourceValues(forKeys: [.fileSizeKey]).fileSize ?? -1
        guard Int64(size) == file.size else { throw ModelInstallError.wrongSize(file: file.file) }
        let handle = try FileHandle(forReadingFrom: url)
        defer { try? handle.close() }
        var hasher = SHA256()
        while let chunk = try handle.read(upToCount: 8 << 20), !chunk.isEmpty {
            try Task.checkCancellation()
            hasher.update(data: chunk)
        }
        let digest = hasher.finalize().map { String(format: "%02x", $0) }.joined()
        guard digest == file.sha256.lowercased() else { throw ModelInstallError.wrongHash(file: file.file) }
    }

    static func extract(_ archive: URL, file: String) async throws {
        let directory = archive.deletingLastPathComponent()
        let name = archive.deletingPathExtension().lastPathComponent
        let staging = directory.appending(path: ".extract-\(UUID().uuidString)")
        defer { try? FileManager.default.removeItem(at: staging) }
        try FileManager.default.createDirectory(at: staging, withIntermediateDirectories: true)

        let process = Process()
        process.executableURL = URL(filePath: "/usr/bin/ditto")
        process.arguments = ["-x", "-k", archive.path(percentEncoded: false), staging.path(percentEncoded: false)]
        let status: Int32 = try await withTaskCancellationHandler {
            try await withCheckedThrowingContinuation { continuation in
                process.terminationHandler = { continuation.resume(returning: $0.terminationStatus) }
                do {
                    try process.run()
                } catch {
                    process.terminationHandler = nil
                    continuation.resume(throwing: error)
                }
            }
        } onCancel: {
            process.terminate()
        }
        try Task.checkCancellation()

        let extracted = staging.appending(path: name)
        guard status == 0, FileManager.default.fileExists(atPath: extracted.path(percentEncoded: false)) else {
            throw ModelInstallError.extractionFailed(file: file)
        }
        let destination = directory.appending(path: name)
        try? FileManager.default.removeItem(at: destination)
        try FileManager.default.moveItem(at: extracted, to: destination)
        try FileManager.default.removeItem(at: archive)
    }
}

final class PartialWriter: NSObject, URLSessionDataDelegate, Sendable {
    private struct State {
        var written: Int64
        var reported: Int64
        var continuation: CheckedContinuation<Void, Error>?
        var failure: Error?
    }

    private let file: String
    private let handle: FileHandle
    private let step: Int64
    private let report: @Sendable (Int64) -> Void
    private let state: Mutex<State>

    init(file: String, handle: FileHandle, offset: Int64, step: Int64, report: @escaping @Sendable (Int64) -> Void) {
        self.file = file
        self.handle = handle
        self.step = step
        self.report = report
        state = Mutex(State(written: offset, reported: offset))
    }

    func run(_ request: URLRequest, session: URLSession) async throws {
        let task = session.dataTask(with: request)
        task.delegate = self
        try await withTaskCancellationHandler {
            try await withCheckedThrowingContinuation { continuation in
                state.withLock { $0.continuation = continuation }
                task.resume()
            }
        } onCancel: {
            task.cancel()
        }
    }

    func urlSession(
        _ session: URLSession,
        dataTask: URLSessionDataTask,
        didReceive response: URLResponse,
        completionHandler: @escaping @Sendable (URLSession.ResponseDisposition) -> Void
    ) {
        let status = (response as? HTTPURLResponse)?.statusCode
        if status == 206 {
            completionHandler(.allow)
            return
        }
        if let status, !(200..<300).contains(status) {
            fail(ModelInstallError.badResponse(file: file, status: status))
            completionHandler(.cancel)
            return
        }
        do {
            try handle.truncate(atOffset: 0)
            state.withLock {
                $0.written = 0
                $0.reported = 0
            }
            completionHandler(.allow)
        } catch {
            fail(error)
            completionHandler(.cancel)
        }
    }

    func urlSession(_ session: URLSession, dataTask: URLSessionDataTask, didReceive data: Data) {
        do {
            try handle.write(contentsOf: data)
        } catch {
            fail(error)
            dataTask.cancel()
            return
        }
        let written: Int64? = state.withLock { state in
            state.written += Int64(data.count)
            guard state.written - state.reported >= step else { return nil }
            state.reported = state.written
            return state.written
        }
        if let written { report(written) }
    }

    func urlSession(_ session: URLSession, task: URLSessionTask, didCompleteWithError error: Error?) {
        let (continuation, failure) = state.withLock { state in
            defer { state.continuation = nil }
            return (state.continuation, state.failure ?? error)
        }
        if let failure {
            continuation?.resume(throwing: failure)
        } else {
            continuation?.resume()
        }
    }

    private func fail(_ error: Error) {
        state.withLock { $0.failure = $0.failure ?? error }
    }
}
