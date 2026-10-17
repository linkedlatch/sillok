import Foundation
#if canImport(Darwin)
import Darwin
#endif

public enum MemoryProbe {
    public static var physicalBytes: Int64 {
        Int64(ProcessInfo.processInfo.physicalMemory)
    }

    public static var availableBytes: Int64 {
        #if os(iOS)
        return Int64(os_proc_available_memory())
        #else
        var stats = vm_statistics64_data_t()
        var count = mach_msg_type_number_t(MemoryLayout<vm_statistics64_data_t>.size / MemoryLayout<integer_t>.size)
        let result = withUnsafeMutablePointer(to: &stats) {
            $0.withMemoryRebound(to: integer_t.self, capacity: Int(count)) {
                host_statistics64(mach_host_self(), HOST_VM_INFO64, $0, &count)
            }
        }
        guard result == KERN_SUCCESS else { return 0 }
        let pages = Int64(stats.free_count) + Int64(stats.inactive_count) + Int64(stats.purgeable_count) + Int64(stats.speculative_count)
        return pages * Int64(getpagesize())
        #endif
    }
}

public final class MemoryPressureMonitor: @unchecked Sendable {
    public enum Level: Sendable {
        case warning
        case critical
    }

    private let source: DispatchSourceMemoryPressure

    public init(handler: @escaping @Sendable (Level) -> Void) {
        source = DispatchSource.makeMemoryPressureSource(eventMask: [.warning, .critical], queue: .main)
        source.setEventHandler { [source] in
            handler(source.data.contains(.critical) ? .critical : .warning)
        }
        source.resume()
    }

    deinit {
        source.cancel()
    }
}
