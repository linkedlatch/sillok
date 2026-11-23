import Foundation
import Observation

@MainActor
@Observable
final class Navigation {
    var importing = false
    var adding: [URL] = []
}
