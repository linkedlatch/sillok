import Foundation

public enum ThermalLevel: Int, Comparable, Sendable {
    case nominal
    case fair
    case serious
    case critical

    public static func < (lhs: ThermalLevel, rhs: ThermalLevel) -> Bool {
        lhs.rawValue < rhs.rawValue
    }
}

public protocol ThermalStateProvider: Sendable {
    var level: ThermalLevel { get }
}
