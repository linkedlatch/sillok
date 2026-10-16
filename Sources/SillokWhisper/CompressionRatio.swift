import Foundation

enum CompressionRatio {
    static func of(_ text: String) -> Float? {
        let data = Data(text.utf8)
        guard !data.isEmpty, let compressed = try? (data as NSData).compressed(using: .zlib), compressed.length > 0 else {
            return nil
        }
        return Float(data.count) / Float(compressed.length)
    }
}
