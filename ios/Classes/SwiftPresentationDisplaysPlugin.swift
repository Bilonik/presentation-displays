import Flutter
import UIKit

public class SwiftPresentationDisplaysPlugin: NSObject, FlutterPlugin {
    var additionalWindows = [UIScreen:UIWindow]()
    var pendingPresentations = [UIScreen:String]()
    var screens = [UIScreen]()
    var flutterEngineChannel:FlutterMethodChannel?=nil
    public static var controllerAdded: ((FlutterViewController)->Void)?

    public override init() {
        super.init()

        screens.append(UIScreen.main)
        registerAlreadyConnectedDisplays()

        NotificationCenter.default.addObserver(forName: UIScreen.didConnectNotification,
                                               object: nil, queue: .main) {
            notification in

            guard let newScreen = notification.object as? UIScreen else {
                return
            }

            self.addScreen(newScreen)
        }

        NotificationCenter.default.addObserver(forName:
                                                UIScreen.didDisconnectNotification,
                                               object: nil,
                                               queue: .main) { notification in
            guard let screen = notification.object as? UIScreen else {
                return
            }

            self.removeScreen(screen)
        }

        if #available(iOS 13.0, *) {
            NotificationCenter.default.addObserver(forName: UIScene.willConnectNotification,
                                                   object: nil,
                                                   queue: .main) { notification in
                guard let windowScene = notification.object as? UIWindowScene,
                      self.isExternalDisplayScene(windowScene) else {
                    return
                }

                self.addScreen(windowScene.screen)
                if let routerName = self.pendingPresentations.removeValue(forKey: windowScene.screen) {
                    self.showPresentation(on: windowScene, routerName: routerName)
                }
            }

            NotificationCenter.default.addObserver(forName: UIScene.didDisconnectNotification,
                                                   object: nil,
                                                   queue: .main) { notification in
                guard let windowScene = notification.object as? UIWindowScene,
                      self.isExternalDisplayScene(windowScene) else {
                    return
                }

                self.removeScreen(windowScene.screen)
            }
        }
    }

    private var usesSceneLifecycle: Bool {
        if #available(iOS 13.0, *) {
            return Bundle.main.object(forInfoDictionaryKey: "UIApplicationSceneManifest") != nil
        }
        return false
    }

    private func addScreen(_ screen: UIScreen) {
        if !screens.contains(screen) {
            screens.append(screen)
        }
    }

    private func removeScreen(_ screen: UIScreen) {
        pendingPresentations.removeValue(forKey: screen)
        if let window = additionalWindows.removeValue(forKey: screen) {
            window.isHidden = true
            if #available(iOS 13.0, *), usesSceneLifecycle {
                window.windowScene = nil
            }
        }
        screens.removeAll { $0 == screen }
    }

    private func registerAlreadyConnectedDisplays() {
        if #available(iOS 13.0, *), usesSceneLifecycle {
            for case let windowScene as UIWindowScene in UIApplication.shared.connectedScenes
                where isExternalDisplayScene(windowScene) {
                addScreen(windowScene.screen)
            }
        } else {
            for screen in UIScreen.screens where screen != UIScreen.main {
                addScreen(screen)
            }
        }
    }

    @available(iOS 13.0, *)
    private func isExternalDisplayScene(_ windowScene: UIWindowScene) -> Bool {
        if #available(iOS 16.0, *) {
            if windowScene.session.role == .windowExternalDisplayNonInteractive {
                return true
            }
        }
        return windowScene.session.role.rawValue == "UIWindowSceneSessionRoleExternalDisplay"
    }

    @available(iOS 13.0, *)
    private func connectedWindowScene(for screen: UIScreen) -> UIWindowScene? {
        return UIApplication.shared.connectedScenes
            .compactMap { $0 as? UIWindowScene }
            .first { $0.screen == screen && isExternalDisplayScene($0) }
    }
    
    public static func register(with registrar: FlutterPluginRegistrar) {
        let channel = FlutterMethodChannel(name: "presentation_displays_plugin", binaryMessenger: registrar.messenger())
        let instance = SwiftPresentationDisplaysPlugin()
        registrar.addMethodCallDelegate(instance, channel: channel)
        
        let eventChannel = FlutterEventChannel(name: "presentation_displays_plugin_events", binaryMessenger: registrar.messenger())
        let displayConnectedStreamHandler = DisplayConnectedStreamHandler()
        eventChannel.setStreamHandler(displayConnectedStreamHandler)
    }

    public func handle(_ call: FlutterMethodCall, result: @escaping FlutterResult) {
        if call.method == "listDisplay" {
            var jsonDisplaysList = "[";
            for i in 0..<screens.count {
                jsonDisplaysList+="{\"displayId\":"+String(i)+", \"name\":\"Screen "+String(i)+"\"},"
            }
            jsonDisplaysList = String(jsonDisplaysList.dropLast())
            jsonDisplaysList+="]"
            jsonDisplaysList = jsonDisplaysList.replacingOccurrences(of: "Screen 0", with: "Built-in Screen")
            result(jsonDisplaysList)
        }
        else if call.method=="showPresentation"{
            guard let arguments = dictionaryArguments(call.arguments),
                  let displayId = arguments["displayId"] as? Int,
                  let routerName = arguments["routerName"] as? String,
                  !routerName.isEmpty else {
                result(false)
                return
            }
            result(showPresentation(index: displayId, routerName: routerName))
        }
        else if call.method=="hidePresentation"{
            guard let arguments = dictionaryArguments(call.arguments),
                  let displayId = arguments["displayId"] as? Int else {
                result(false)
                return
            }
            result(hidePresentation(index: displayId))
        }
        else if call.method=="transferDataToPresentation"{
            guard let channel = self.flutterEngineChannel else {
                result(false)
                return
            }
            channel.invokeMethod("DataTransfer", arguments: call.arguments)
            result(true)
        }
        else
        {
            result(FlutterMethodNotImplemented)
        }

    }

    private func dictionaryArguments(_ arguments: Any?) -> [String: Any]? {
        if let dictionary = arguments as? [String: Any] {
            return dictionary
        }
        guard let string = arguments as? String,
              let data = string.data(using: .utf8) else {
            return nil
        }
        return (try? JSONSerialization.jsonObject(with: data)) as? [String: Any]
    }

    private func showPresentation(index:Int, routerName:String) -> Bool
    {
        guard index > 0 && index < screens.count else {
            return false
        }

        let screen = screens[index]
        if #available(iOS 13.0, *), usesSceneLifecycle {
            guard let windowScene = connectedWindowScene(for: screen) else {
                pendingPresentations[screen] = routerName
                return true
            }

            showPresentation(on: windowScene, routerName: routerName)
            return true
        }

        let window = additionalWindows[screen] ?? createLegacyWindow(for: screen)
        configure(window: window, routerName: routerName)
        return true
    }

    private func createLegacyWindow(for screen: UIScreen) -> UIWindow {
        let window = UIWindow(frame: screen.bounds)
        window.screen = screen
        additionalWindows[screen] = window
        return window
    }

    @available(iOS 13.0, *)
    private func showPresentation(on windowScene: UIWindowScene, routerName: String) {
        let screen = windowScene.screen
        let window: UIWindow
        if let existingWindow = additionalWindows[screen], existingWindow.windowScene === windowScene {
            window = existingWindow
        } else {
            additionalWindows[screen]?.windowScene = nil
            window = UIWindow(windowScene: windowScene)
            additionalWindows[screen] = window
        }

        configure(window: window, routerName: routerName)
    }

    private func configure(window: UIWindow, routerName: String) {
        if window.rootViewController == nil || !(window.rootViewController is FlutterViewController) {
            let extVC = FlutterViewController(project: nil, initialRoute: routerName, nibName: nil, bundle: nil)

            // Flutter's UIScene lifecycle registers plugins on every
            // implicit engine before the view controller initializer
            // returns. Running the legacy registrant callback again
            // raises a "Duplicate plugin key" assertion.
            if !extVC.hasPlugin("PresentationDisplaysPlugin") {
                SwiftPresentationDisplaysPlugin.controllerAdded?(extVC)
            }
            window.rootViewController = extVC

            flutterEngineChannel = FlutterMethodChannel(name: "presentation_displays_plugin_engine", binaryMessenger: extVC.binaryMessenger)
        }

        window.makeKeyAndVisible()
    }

    private func hidePresentation(index:Int) -> Bool
    {
        guard index > 0 && index < screens.count else {
            return false
        }

        let screen = screens[index]
        let wasPending = pendingPresentations.removeValue(forKey: screen) != nil
        if let window = additionalWindows[screen] {
            window.isHidden = true
            if #available(iOS 13.0, *), usesSceneLifecycle {
                window.windowScene = nil
                additionalWindows.removeValue(forKey: screen)
            }
            return true
        }
        return wasPending
    }

}

class DisplayConnectedStreamHandler: NSObject, FlutterStreamHandler{
    var sink: FlutterEventSink?
    var didConnectObserver: NSObjectProtocol?
    var didDisconnectObserver: NSObjectProtocol?

    func onListen(withArguments arguments: Any?, eventSink events: @escaping FlutterEventSink) -> FlutterError? {
        sink = events
        didConnectObserver = NotificationCenter.default.addObserver(forName: UIScreen.didConnectNotification,
                            object: nil, queue: nil) { (notification) in
            guard let sink = self.sink else { return }
            sink(1)
           }
        didDisconnectObserver = NotificationCenter.default.addObserver(forName: UIScreen.didDisconnectNotification,
                            object: nil, queue: nil) { (notification) in
            guard let sink = self.sink else { return }
            sink(0)
           }
        return nil
    }

    func onCancel(withArguments arguments: Any?) -> FlutterError? {
        sink = nil
        if (didConnectObserver != nil){
            NotificationCenter.default.removeObserver(didConnectObserver!)
        }
        if (didDisconnectObserver != nil){
            NotificationCenter.default.removeObserver(didDisconnectObserver!)
        }
        return nil
    }
}
