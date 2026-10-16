import Foundation

final class StubServer: URLProtocol, @unchecked Sendable {
    struct Route {
        var status = 200
        var body = Data()
        var failAfter: Int?
        var ignoresRange = false
    }

    nonisolated(unsafe) static var routes: [String: Route] = [:]
    nonisolated(unsafe) static var requests: [URLRequest] = []

    static var configuration: URLSessionConfiguration {
        let configuration = URLSessionConfiguration.ephemeral
        configuration.protocolClasses = [StubServer.self]
        return configuration
    }

    static func reset() {
        routes = [:]
        requests = []
    }

    override class func canInit(with request: URLRequest) -> Bool { true }
    override class func canonicalRequest(for request: URLRequest) -> URLRequest { request }

    override func startLoading() {
        Self.requests.append(request)
        let url = request.url!
        guard var route = Self.routes[url.absoluteString] else {
            respond(status: 404, body: Data(), url: url)
            return
        }
        if !route.ignoresRange,
           let range = request.value(forHTTPHeaderField: "Range"),
           let start = Int(range.dropFirst("bytes=".count).dropLast()) {
            route.status = 206
            route.body = route.body.subdata(in: start..<route.body.count)
        }
        let response = HTTPURLResponse(url: url, statusCode: route.status, httpVersion: "HTTP/1.1",
                                       headerFields: ["Content-Length": "\(route.body.count)"])!
        client?.urlProtocol(self, didReceive: response, cacheStoragePolicy: .notAllowed)
        if let failAfter = route.failAfter {
            client?.urlProtocol(self, didLoad: route.body.prefix(failAfter))
            client?.urlProtocol(self, didFailWithError: URLError(.networkConnectionLost))
            Self.routes[url.absoluteString]?.failAfter = nil
        } else {
            client?.urlProtocol(self, didLoad: route.body)
            client?.urlProtocolDidFinishLoading(self)
        }
    }

    override func stopLoading() {}

    private func respond(status: Int, body: Data, url: URL) {
        let response = HTTPURLResponse(url: url, statusCode: status, httpVersion: "HTTP/1.1", headerFields: nil)!
        client?.urlProtocol(self, didReceive: response, cacheStoragePolicy: .notAllowed)
        client?.urlProtocol(self, didLoad: body)
        client?.urlProtocolDidFinishLoading(self)
    }
}
