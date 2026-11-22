import AppKit
import UserNotifications

@MainActor
enum Notifications {
    static func requestPermission() {
        UNUserNotificationCenter.current().requestAuthorization(options: [.alert, .sound]) { _, _ in }
    }

    static func finished(_ title: String) {
        post(String(localized: "Transcription Finished"), body: title)
    }

    static func failed(_ title: String, message: String) {
        post(String(localized: "Transcription Failed"), body: "\(title): \(message)")
    }

    private static func post(_ title: String, body: String) {
        guard !NSApp.isActive else { return }
        let content = UNMutableNotificationContent()
        content.title = title
        content.body = body
        content.sound = .default
        UNUserNotificationCenter.current().add(UNNotificationRequest(identifier: UUID().uuidString, content: content,
                                                                     trigger: nil))
    }
}
