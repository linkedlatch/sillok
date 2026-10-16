import Foundation
import Network

final class TestHTTPServer: @unchecked Sendable {
    struct Route {
        var body = Data()
        var cutAfter: Int?
        var stalls = false
    }

    struct Request: Sendable {
        var path: String
        var headers: [String: String]
    }

    private let listener: NWListener
    private let queue = DispatchQueue(label: "TestHTTPServer")
    private let lock = NSLock()
    private var routes: [String: Route] = [:]
    private var log: [Request] = []
    private var connections: [NWConnection] = []

    init() throws {
        let parameters = NWParameters.tcp
        parameters.requiredLocalEndpoint = .hostPort(host: .ipv4(.loopback), port: .any)
        listener = try NWListener(using: parameters)
    }

    deinit {
        listener.cancel()
        connections.forEach { $0.cancel() }
    }

    var requests: [Request] { lock.withLock { log } }

    func route(_ path: String, _ route: Route) {
        lock.withLock { routes[path] = route }
    }

    func url(_ path: String) -> String {
        "http://127.0.0.1:\(listener.port!.rawValue)\(path)"
    }

    func start() async throws {
        try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<Void, Error>) in
            listener.stateUpdateHandler = { [listener] state in
                switch state {
                case .ready: continuation.resume()
                case .failed(let error): continuation.resume(throwing: error)
                default: return
                }
                listener.stateUpdateHandler = nil
            }
            listener.newConnectionHandler = { [weak self] connection in self?.accept(connection) }
            listener.start(queue: queue)
        }
    }

    private func accept(_ connection: NWConnection) {
        lock.withLock { connections.append(connection) }
        connection.start(queue: queue)
        receive(connection, buffer: Data())
    }

    private func receive(_ connection: NWConnection, buffer: Data) {
        connection.receive(minimumIncompleteLength: 1, maximumLength: 65536) { [weak self] data, _, isComplete, error in
            guard let self else { return }
            var buffer = buffer
            if let data { buffer.append(data) }
            if let end = buffer.range(of: Data("\r\n\r\n".utf8)) {
                respond(connection, head: String(decoding: buffer[..<end.lowerBound], as: UTF8.self))
            } else if !isComplete, error == nil {
                receive(connection, buffer: buffer)
            }
        }
    }

    private func respond(_ connection: NWConnection, head: String) {
        let lines = head.components(separatedBy: "\r\n")
        let path = lines.first?.split(separator: " ").dropFirst().first.map(String.init) ?? "/"
        var headers: [String: String] = [:]
        for line in lines.dropFirst() {
            guard let colon = line.firstIndex(of: ":") else { continue }
            headers[line[..<colon].lowercased()] = line[line.index(after: colon)...].trimmingCharacters(in: .whitespaces)
        }
        let route = lock.withLock { () -> Route? in
            log.append(Request(path: path, headers: headers))
            let route = routes[path]
            routes[path]?.cutAfter = nil
            return route
        }

        guard let route else {
            send(connection, status: "404 Not Found", headers: ["Content-Length": "0"], body: Data(), close: true)
            return
        }
        var status = "200 OK"
        var body = route.body
        var fields = [
            "Accept-Ranges": "bytes",
            "ETag": "\"sillok-test\"",
            "Last-Modified": "Mon, 01 Jan 2024 00:00:00 GMT",
            "Content-Type": "application/octet-stream",
        ]
        if let range = headers["range"], range.hasPrefix("bytes="), range.hasSuffix("-"),
           let start = Int(range.dropFirst("bytes=".count).dropLast()), start < body.count {
            status = "206 Partial Content"
            fields["Content-Range"] = "bytes \(start)-\(body.count - 1)/\(body.count)"
            body = body.subdata(in: start..<body.count)
        }
        fields["Content-Length"] = "\(body.count)"
        if let cutAfter = route.cutAfter {
            send(connection, status: status, headers: fields, body: body.prefix(cutAfter), close: !route.stalls)
        } else {
            send(connection, status: status, headers: fields, body: body, close: true)
        }
    }

    private func send(_ connection: NWConnection, status: String, headers: [String: String], body: Data, close: Bool) {
        var head = "HTTP/1.1 \(status)\r\nConnection: close\r\n"
        for (name, value) in headers { head += "\(name): \(value)\r\n" }
        var data = Data((head + "\r\n").utf8)
        data.append(body)
        connection.send(content: data, completion: .contentProcessed { _ in
            if close { connection.cancel() }
        })
    }
}
