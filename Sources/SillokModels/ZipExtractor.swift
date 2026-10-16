import Compression
import Foundation

public enum ZipExtractor {
    private struct Entry {
        var name: String
        var method: UInt16
        var compressedSize: Int
        var uncompressedSize: Int
        var localHeaderOffset: UInt64
    }

    public static func extract(_ archive: URL, to destination: URL) throws {
        let handle = try FileHandle(forReadingFrom: archive)
        defer { try? handle.close() }
        let fileSize = try handle.seekToEnd()

        for entry in try centralDirectory(handle, fileSize: fileSize) where !entry.name.hasPrefix("__MACOSX/") {
            let target = try safeURL(for: entry.name, in: destination)
            if entry.name.hasSuffix("/") {
                try FileManager.default.createDirectory(at: target, withIntermediateDirectories: true)
                continue
            }
            try FileManager.default.createDirectory(at: target.deletingLastPathComponent(), withIntermediateDirectories: true)
            try handle.seek(toOffset: entry.localHeaderOffset)
            let local = try read(handle, 30)
            guard local.uint32(at: 0) == 0x0403_4b50 else { throw ModelError.invalidArchive("local header") }
            try handle.seek(toOffset: entry.localHeaderOffset + 30 + UInt64(local.uint16(at: 26)) + UInt64(local.uint16(at: 28)))

            FileManager.default.createFile(atPath: target.path, contents: nil)
            let output = try FileHandle(forWritingTo: target)
            defer { try? output.close() }
            switch entry.method {
            case 0:
                try copy(handle, to: output, count: entry.compressedSize)
            case 8:
                try inflate(handle, to: output, count: entry.compressedSize)
            default:
                throw ModelError.invalidArchive("method \(entry.method)")
            }
            guard try output.offset() == UInt64(entry.uncompressedSize) else {
                throw ModelError.invalidArchive("size of \(entry.name)")
            }
        }
    }

    private static func centralDirectory(_ handle: FileHandle, fileSize: UInt64) throws -> [Entry] {
        let tailLength = min(fileSize, 65_557)
        try handle.seek(toOffset: fileSize - tailLength)
        let tail = try read(handle, Int(tailLength))
        guard let end = stride(from: tail.count - 22, through: 0, by: -1).first(where: { tail.uint32(at: $0) == 0x0605_4b50 }) else {
            throw ModelError.invalidArchive("end of central directory")
        }
        let count = Int(tail.uint16(at: end + 10))
        let size = Int(tail.uint32(at: end + 12))
        let offset = UInt64(tail.uint32(at: end + 16))
        guard count != 0xFFFF, size != 0xFFFF_FFFF, offset != 0xFFFF_FFFF else { throw ModelError.invalidArchive("zip64") }

        try handle.seek(toOffset: offset)
        let directory = try read(handle, size)
        var entries: [Entry] = []
        var position = 0
        for _ in 0..<count {
            guard directory.uint32(at: position) == 0x0201_4b50 else { throw ModelError.invalidArchive("central header") }
            let nameLength = Int(directory.uint16(at: position + 28))
            let extraLength = Int(directory.uint16(at: position + 30))
            let commentLength = Int(directory.uint16(at: position + 32))
            let nameStart = directory.startIndex + position + 46
            entries.append(Entry(
                name: String(decoding: directory[nameStart..<nameStart + nameLength], as: UTF8.self),
                method: directory.uint16(at: position + 10),
                compressedSize: Int(directory.uint32(at: position + 20)),
                uncompressedSize: Int(directory.uint32(at: position + 24)),
                localHeaderOffset: UInt64(directory.uint32(at: position + 42))
            ))
            position += 46 + nameLength + extraLength + commentLength
        }
        return entries
    }

    private static func safeURL(for name: String, in destination: URL) throws -> URL {
        let components = name.split(separator: "/", omittingEmptySubsequences: true)
        guard !name.hasPrefix("/"), !components.isEmpty, !components.contains("..") else {
            throw ModelError.invalidArchive("unsafe path \(name)")
        }
        return components.reduce(destination) { $0.appendingPathComponent(String($1)) }
    }

    private static func read(_ handle: FileHandle, _ count: Int) throws -> Data {
        let data = try handle.read(upToCount: count) ?? Data()
        guard data.count == count else { throw ModelError.invalidArchive("truncated") }
        return data
    }

    private static func copy(_ input: FileHandle, to output: FileHandle, count: Int) throws {
        var remaining = count
        while remaining > 0 {
            let data = try read(input, min(remaining, 4 << 20))
            try output.write(contentsOf: data)
            remaining -= data.count
        }
    }

    private static func inflate(_ input: FileHandle, to output: FileHandle, count: Int) throws {
        let bufferSize = 4 << 20
        let source = UnsafeMutablePointer<UInt8>.allocate(capacity: bufferSize)
        let destination = UnsafeMutablePointer<UInt8>.allocate(capacity: bufferSize)
        defer {
            source.deallocate()
            destination.deallocate()
        }
        var stream = compression_stream(dst_ptr: destination, dst_size: 0, src_ptr: UnsafePointer(source), src_size: 0, state: nil)
        guard compression_stream_init(&stream, COMPRESSION_STREAM_DECODE, COMPRESSION_ZLIB) == COMPRESSION_STATUS_OK else {
            throw ModelError.invalidArchive("inflate init")
        }
        defer { compression_stream_destroy(&stream) }

        var remaining = count
        var status = COMPRESSION_STATUS_OK
        repeat {
            if stream.src_size == 0, remaining > 0 {
                let data = try read(input, min(remaining, bufferSize))
                data.copyBytes(to: source, count: data.count)
                stream.src_ptr = UnsafePointer(source)
                stream.src_size = data.count
                remaining -= data.count
            }
            stream.dst_ptr = destination
            stream.dst_size = bufferSize
            let flags = remaining == 0 ? Int32(COMPRESSION_STREAM_FINALIZE.rawValue) : 0
            status = compression_stream_process(&stream, flags)
            guard status != COMPRESSION_STATUS_ERROR else { throw ModelError.invalidArchive("inflate") }
            let produced = bufferSize - stream.dst_size
            if produced > 0 {
                try output.write(contentsOf: Data(bytes: destination, count: produced))
            }
        } while status == COMPRESSION_STATUS_OK
    }
}

private extension Data {
    func uint16(at offset: Int) -> UInt16 {
        let i = startIndex + offset
        return UInt16(self[i]) | UInt16(self[i + 1]) << 8
    }

    func uint32(at offset: Int) -> UInt32 {
        let i = startIndex + offset
        return UInt32(self[i]) | UInt32(self[i + 1]) << 8 | UInt32(self[i + 2]) << 16 | UInt32(self[i + 3]) << 24
    }
}
