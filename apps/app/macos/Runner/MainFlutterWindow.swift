import Cocoa
import FlutterMacOS

class MainFlutterWindow: NSWindow {
  // Held while files are transcribed (lib/data/keep_awake.dart): keeps the Mac
  // from idle sleep and the app out of App Nap.
  private var activity: NSObjectProtocol?

  override func awakeFromNib() {
    let flutterViewController = FlutterViewController()
    let windowFrame = self.frame
    self.contentViewController = flutterViewController
    self.setFrame(windowFrame, display: true)

    RegisterGeneratedPlugins(registry: flutterViewController)

    let keepAwake = FlutterMethodChannel(
      name: "sillok/keep_awake", binaryMessenger: flutterViewController.engine.binaryMessenger)
    keepAwake.setMethodCallHandler { [weak self] call, result in
      guard let self = self else { return result(nil) }
      switch call.method {
      case "hold":
        if self.activity == nil {
          self.activity = ProcessInfo.processInfo.beginActivity(
            options: .userInitiated, reason: "Transcribing recordings")
        }
        result(nil)
      case "release":
        if let activity = self.activity {
          ProcessInfo.processInfo.endActivity(activity)
          self.activity = nil
        }
        result(nil)
      default:
        result(FlutterMethodNotImplemented)
      }
    }

    super.awakeFromNib()
  }
}
