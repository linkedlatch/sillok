import Foundation

enum TimeFormat {
    static func clock(_ seconds: TimeInterval) -> String {
        let total = Int(max(seconds, 0))
        return total >= 3600
            ? String(format: "%d:%02d:%02d", total / 3600, total / 60 % 60, total % 60)
            : String(format: "%02d:%02d", total / 60, total % 60)
    }
}
