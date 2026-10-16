import Foundation
import SillokCore

public struct ThermalMonitor: ThermalStateProvider {
    public init() {}

    public var level: ThermalLevel {
        switch ProcessInfo.processInfo.thermalState {
        case .nominal: .nominal
        case .fair: .fair
        case .serious: .serious
        case .critical: .critical
        @unknown default: .serious
        }
    }
}
