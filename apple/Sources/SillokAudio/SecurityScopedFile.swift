import Foundation

public enum SecurityScopedFile {
    public enum Resolution {
        case available(URL, refreshedBookmark: Data?)
        case missing
    }

    public static func bookmark(for url: URL) throws -> Data {
        let accessing = url.startAccessingSecurityScopedResource()
        defer { if accessing { url.stopAccessingSecurityScopedResource() } }
        return try url.bookmarkData(options: creationOptions, includingResourceValuesForKeys: nil, relativeTo: nil)
    }

    public static func resolve(_ bookmark: Data) -> Resolution {
        var stale = false
        guard let url = try? URL(resolvingBookmarkData: bookmark, options: resolutionOptions, relativeTo: nil, bookmarkDataIsStale: &stale) else {
            return .missing
        }
        let accessing = url.startAccessingSecurityScopedResource()
        defer { if accessing { url.stopAccessingSecurityScopedResource() } }
        guard FileManager.default.fileExists(atPath: url.path) else { return .missing }
        return .available(url, refreshedBookmark: stale ? try? Self.bookmark(for: url) : nil)
    }

    public static func withAccess<T>(to url: URL, _ body: () async throws -> T) async rethrows -> T {
        let accessing = url.startAccessingSecurityScopedResource()
        defer { if accessing { url.stopAccessingSecurityScopedResource() } }
        return try await body()
    }

    #if os(macOS)
    private static let creationOptions: URL.BookmarkCreationOptions = [.withSecurityScope]
    private static let resolutionOptions: URL.BookmarkResolutionOptions = [.withSecurityScope]
    #else
    private static let creationOptions: URL.BookmarkCreationOptions = []
    private static let resolutionOptions: URL.BookmarkResolutionOptions = []
    #endif
}
