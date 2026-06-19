import AppKit
import Darwin
import Foundation

@main
@MainActor
final class AppDelegate: NSObject, NSApplicationDelegate, NSMenuDelegate {
    private let statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
    private let controller = LegoClawdController()
    private var refreshTimer: Timer?
    private lazy var menuBarImage: NSImage? = loadMenuBarImage()

    private let bridgeItem = NSMenuItem(title: "Bridge: stopped", action: nil, keyEquivalent: "")
    private let connectionItem = NSMenuItem(title: "Serial: checking...", action: nil, keyEquivalent: "")
    private let aiStateItem = NSMenuItem(title: "State: unknown", action: nil, keyEquivalent: "")
    private let lastActionItem = NSMenuItem(title: "Last: none", action: nil, keyEquivalent: "")
    private let connectBridgeItem = NSMenuItem(title: "Connect Bridge", action: nil, keyEquivalent: "c")
    private let disconnectBridgeItem = NSMenuItem(title: "Disconnect Bridge", action: nil, keyEquivalent: "d")
    private let quietModeItem = NSMenuItem(title: "Quiet Mode", action: nil, keyEquivalent: "m")
    private let testIdleItem = NSMenuItem(title: "Test Idle", action: nil, keyEquivalent: "1")
    private let testWorkingItem = NSMenuItem(title: "Test Working", action: nil, keyEquivalent: "2")
    private let testApprovalItem = NSMenuItem(title: "Test Approval", action: nil, keyEquivalent: "3")
    private let testDoneItem = NSMenuItem(title: "Test Done", action: nil, keyEquivalent: "4")
    private let testErrorItem = NSMenuItem(title: "Test Error", action: nil, keyEquivalent: "5")
    private let selfTestItem = NSMenuItem(title: "Self-Test", action: nil, keyEquivalent: "s")

    private var testItems: [NSMenuItem] {
        [testIdleItem, testWorkingItem, testApprovalItem, testDoneItem, testErrorItem, selfTestItem]
    }

    static func main() {
        let app = NSApplication.shared
        let delegate = AppDelegate()
        app.delegate = delegate
        app.run()
    }

    func applicationDidFinishLaunching(_ notification: Notification) {
        setupMenu()
        refreshStatus()
        showLaunchDockCue()
        refreshTimer = Timer.scheduledTimer(withTimeInterval: 2.0, repeats: true) { [weak self] _ in
            Task { @MainActor in
                self?.refreshStatus()
            }
        }
    }

    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool) -> Bool {
        statusItem.button?.performClick(nil)
        return false
    }

    private func showLaunchDockCue() {
        NSApp.setActivationPolicy(.regular)
        NSApp.requestUserAttention(.informationalRequest)
        DispatchQueue.main.asyncAfter(deadline: .now() + 1.6) {
            NSApp.setActivationPolicy(.accessory)
        }
    }

    private func setupMenu() {
        statusItem.length = 26
        if let menuBarImage {
            statusItem.button?.image = menuBarImage
            statusItem.button?.imagePosition = .imageOnly
        } else {
            statusItem.button?.title = "Clawd"
        }
        statusItem.button?.toolTip = "Lego Clawd"

        let menu = NSMenu()
        menu.delegate = self
        menu.autoenablesItems = false
        menu.addItem(groupHeader("Status"))
        menu.addItem(bridgeItem)
        menu.addItem(connectionItem)
        menu.addItem(aiStateItem)
        menu.addItem(lastActionItem)
        menu.addItem(.separator())

        menu.addItem(groupHeader("Actions"))
        menu.addItem(connectBridgeItem)
        menu.addItem(disconnectBridgeItem)
        menu.addItem(quietModeItem)
        menu.addItem(.separator())

        menu.addItem(groupHeader("Test"))
        menu.addItem(testIdleItem)
        menu.addItem(testWorkingItem)
        menu.addItem(testApprovalItem)
        menu.addItem(testDoneItem)
        menu.addItem(testErrorItem)
        menu.addItem(selfTestItem)
        menu.addItem(.separator())

        menu.addItem(NSMenuItem(title: "Quit", action: #selector(quit), keyEquivalent: "q"))

        menu.items.forEach { $0.target = self }
        bridgeItem.isEnabled = true
        bridgeItem.target = self
        bridgeItem.action = #selector(toggleBridge)
        bridgeItem.toolTip = "Toggle bridge connection"
        connectBridgeItem.action = #selector(connectBridge)
        disconnectBridgeItem.action = #selector(disconnectBridge)
        quietModeItem.action = #selector(toggleQuietMode)
        testIdleItem.action = #selector(testIdle)
        testWorkingItem.action = #selector(testWorking)
        testApprovalItem.action = #selector(testApproval)
        testDoneItem.action = #selector(testDone)
        testErrorItem.action = #selector(testError)
        selfTestItem.action = #selector(selfTest)

        [connectionItem, aiStateItem, lastActionItem].forEach { item in
            item.isEnabled = false
            item.target = nil
            item.action = nil
        }
        statusItem.menu = menu
    }

    func menuWillOpen(_ menu: NSMenu) {
        refreshStatus()
    }

    private func groupHeader(_ title: String) -> NSMenuItem {
        let item = NSMenuItem(title: title, action: nil, keyEquivalent: "")
        item.isEnabled = false
        item.attributedTitle = NSAttributedString(
            string: title,
            attributes: [
                .foregroundColor: NSColor.secondaryLabelColor,
                .font: NSFont.menuFont(ofSize: 11)
            ]
        )
        return item
    }

    private func refreshStatus() {
        let snapshot = controller.snapshot()
        if menuBarImage != nil {
            statusItem.button?.alphaValue = snapshot.bridgeRunning ? 1.0 : 0.35
        } else {
            statusItem.button?.attributedTitle = fallbackStatusTitle(snapshot)
        }
        bridgeItem.isEnabled = snapshot.isConnected || snapshot.bridgeRunning
        connectBridgeItem.isEnabled = snapshot.isConnected && !snapshot.bridgeRunning
        disconnectBridgeItem.isEnabled = snapshot.bridgeRunning
        quietModeItem.isEnabled = snapshot.isConnected
        quietModeItem.state = controller.quietMode ? .on : .off
        testItems.forEach { $0.isEnabled = snapshot.isConnected }
        bridgeItem.attributedTitle = menuStatusTitle(
            dot: snapshot.bridgeRunning ? "●" : "●",
            dotColor: bridgeDotColor(snapshot),
            label: "Bridge",
            value: snapshot.bridgeText,
            textColor: bridgeItem.isEnabled ? .labelColor : .secondaryLabelColor
        )
        connectionItem.title = "Serial: \(snapshot.connectionText)"
        aiStateItem.title = "State: \(snapshot.aiState)"
        lastActionItem.title = "Last: \(snapshot.lastAction)"
    }

    private func loadMenuBarImage() -> NSImage? {
        let urls: [URL?] = [
            Bundle.module.url(forResource: "MenuBarIconTemplate", withExtension: "png"),
            Bundle.main.url(forResource: "MenuBarIconTemplate", withExtension: "png"),
            Bundle.main.resourceURL?.appendingPathComponent("MenuBarIconTemplate.png"),
        ]

        for url in urls.compactMap({ $0 }) {
            if let image = NSImage(contentsOf: url) {
                image.isTemplate = true
                image.size = NSSize(width: 24, height: 16)
                return image
            }
        }
        return nil
    }

    private func fallbackStatusTitle(_ snapshot: StatusSnapshot) -> NSAttributedString {
        let title = NSMutableAttributedString(string: "Clawd ")
        let dotColor: NSColor
        if snapshot.bridgeRunning && snapshot.isConnected {
            dotColor = .systemGreen
        } else if snapshot.isConnected {
            dotColor = .systemOrange
        } else {
            dotColor = .systemGray
        }
        title.append(NSAttributedString(
            string: "●",
            attributes: [.foregroundColor: dotColor]
        ))
        return title
    }

    private func menuStatusTitle(
        dot: String,
        dotColor: NSColor,
        label: String,
        value: String,
        textColor: NSColor = .labelColor
    ) -> NSAttributedString {
        let result = NSMutableAttributedString()
        let textAttributes: [NSAttributedString.Key: Any] = [
            .foregroundColor: textColor,
            .font: NSFont.menuFont(ofSize: NSFont.systemFontSize)
        ]
        result.append(NSAttributedString(
            string: "\(dot) ",
            attributes: [.foregroundColor: dotColor]
        ))
        result.append(NSAttributedString(
            string: "\(label): ",
            attributes: textAttributes
        ))
        result.append(NSAttributedString(
            string: value,
            attributes: textAttributes
        ))
        return result
    }

    private func bridgeDotColor(_ snapshot: StatusSnapshot) -> NSColor {
        if snapshot.bridgeRunning {
            return .systemGreen
        }
        return snapshot.isConnected ? .systemRed : .systemGray
    }

    private func runAction(_ name: String, _ action: @escaping () -> Void) {
        controller.lastAction = "\(name): running"
        refreshStatus()
        DispatchQueue.global(qos: .userInitiated).async {
            action()
            Task { @MainActor in
                self.refreshStatus()
            }
        }
    }

    @objc private func connectBridge() {
        runAction("Connect") {
            self.controller.startBridge()
        }
    }

    @objc private func toggleBridge() {
        if disconnectBridgeItem.isEnabled {
            runAction("Disconnect") {
                self.controller.stopBridge(notifyDevice: true)
            }
        } else if connectBridgeItem.isEnabled {
            runAction("Connect") {
                self.controller.startBridge()
            }
        } else {
            refreshStatus()
        }
    }

    @objc private func disconnectBridge() {
        runAction("Disconnect") {
            self.controller.stopBridge(notifyDevice: true)
        }
    }

    @objc private func toggleQuietMode() {
        let enabled = !controller.quietMode
        controller.quietMode = enabled
        quietModeItem.state = enabled ? .on : .off
        runAction(enabled ? "Quiet On" : "Quiet Off") {
            self.controller.sendQuietMode(enabled)
        }
    }

    @objc private func testIdle() {
        runAction("Idle") {
            self.controller.sendState("idle")
        }
    }

    @objc private func testWorking() {
        runAction("Working") {
            self.controller.sendState("working")
        }
    }

    @objc private func testApproval() {
        runAction("Approval") {
            self.controller.approvalTest(seconds: 6)
        }
    }

    @objc private func testDone() {
        runAction("Done") {
            self.controller.sendState("waiting")
        }
    }

    @objc private func testError() {
        runAction("Error") {
            self.controller.sendState("error")
        }
    }

    @objc private func selfTest() {
        runAction("Self-Test") {
            self.controller.selfTest()
        }
    }

    @objc private func quit() {
        NSApplication.shared.terminate(nil)
    }
}

struct StatusSnapshot {
    let isConnected: Bool
    let bridgeRunning: Bool
    let connectionText: String
    let aiState: String
    let bridgeText: String
    let lastAction: String
}

final class LegoClawdController {
    private let projectRoot: URL
    private let bridgeScript: URL
    private let bridgePython: URL
    private let bridgeControl: URL

    var lastAction = "none"
    var quietMode: Bool {
        get { UserDefaults.standard.bool(forKey: "quietMode") }
        set { UserDefaults.standard.set(newValue, forKey: "quietMode") }
    }

    init() {
        let envRoot = ProcessInfo.processInfo.environment["LEGO_CLAWD_PROJECT_ROOT"]
        let fallbackRoot = FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent("Library/Mobile Documents/com~apple~CloudDocs/toolkit/lego-clawd")
            .path
        let rootPath = envRoot?.isEmpty == false ? envRoot! : fallbackRoot
        projectRoot = URL(fileURLWithPath: rootPath)
        bridgeScript = projectRoot.appendingPathComponent("tools/run-bridge.sh")
        bridgeControl = projectRoot.appendingPathComponent("tools/bridge-control.py")
        bridgePython = FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent(".platformio/penv/bin/python")
    }

    func snapshot() -> StatusSnapshot {
        let ports = serialPorts()
        let state = readAIState()
        let bridgeRunning = isBridgeRunning()
        let portText = ports.isEmpty ? "no serial device" : ports.joined(separator: ", ")
        return StatusSnapshot(
            isConnected: !ports.isEmpty,
            bridgeRunning: bridgeRunning,
            connectionText: portText,
            aiState: state,
            bridgeText: bridgeRunning ? "running" : "stopped",
            lastAction: lastAction
        )
    }

    func startBridge() {
        let result = runCommand(
            bridgePython.path,
            [bridgeControl.path, "start"],
            timeout: 8
        )
        if result != 0 {
            lastAction = "Connect failed: bridge-control \(result)"
            return
        }
        lastAction = isBridgeRunning() ? "Connect: bridge started" : "Connect failed: bridge exited"
    }

    func stopBridge(notifyDevice: Bool = false) {
        _ = runCommand(bridgePython.path, [bridgeControl.path, "stop"], timeout: 8)
        if notifyDevice && !serialPorts().isEmpty {
            let result = runCommand(
                bridgeScript.path,
                ["--once", "--state", "disconnected"],
                timeout: 15
            )
            lastAction = result == 0 ? "Disconnect: device notified" : "Disconnect: serial released"
            return
        }
        lastAction = "Disconnect: serial released"
    }

    func sendState(_ state: String) {
        runWithBridgePaused(label: "State \(state)") {
            _ = self.runCommand(
                self.bridgeScript.path,
                ["--once", "--state", state],
                timeout: 15
            )
        }
    }

    func sendQuietMode(_ enabled: Bool) {
        runWithBridgePaused(label: enabled ? "Quiet on" : "Quiet off") {
            _ = self.runCommand(
                self.bridgeScript.path,
                ["--once", "--quiet-mode", enabled ? "true" : "false"],
                timeout: 15
            )
        }
    }

    func approvalTest(seconds: Int) {
        runWithBridgePaused(label: "Approval test") {
            _ = self.runCommand(
                self.bridgeScript.path,
                ["--approval-test", "\(seconds)"],
                timeout: seconds + 15
            )
        }
    }

    func selfTest() {
        runWithBridgePaused(label: "Self-test") {
            _ = self.runCommand(
                self.bridgeScript.path,
                ["--once", "--self-test"],
                timeout: 20
            )
        }
    }

    private func runWithBridgePaused(label: String, action: () -> Void) {
        let wasRunning = isBridgeRunning()
        if wasRunning {
            stopBridge(notifyDevice: false)
        }
        action()
        lastAction = "\(label): sent"
        if wasRunning {
            startBridge()
        }
    }

    private func isBridgeRunning() -> Bool {
        guard let output = commandOutput(bridgePython.path,
                                         [bridgeControl.path, "status-json"],
                                         timeout: 2),
              let data = output.data(using: .utf8),
              let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              let running = json["running"] as? Bool else {
            return false
        }
        return running
    }

    private func serialPorts() -> [String] {
        let prefixes = ["cu.usbmodem", "cu.usbserial", "cu.wchusbserial", "cu.SLAB_USBtoUART"]
        guard let entries = try? FileManager.default.contentsOfDirectory(atPath: "/dev") else {
            return []
        }
        return entries
            .filter { entry in prefixes.contains { entry.hasPrefix($0) } }
            .map { "/dev/\($0)" }
            .sorted()
    }

    private func readAIState() -> String {
        let url = FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent(".lego-clawd/ai-status.json")
        guard let data = try? Data(contentsOf: url),
              let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else {
            return "unknown"
        }
        if let hookEvent = json["hookEvent"] as? String, hookEvent == "PermissionRequest" {
            return "pending"
        }
        if let state = json["state"] as? String {
            return state
        }
        if let state = json["aiState"] as? String {
            return state
        }
        if let waiting = json["waiting"] as? Bool, waiting {
            return "waiting"
        }
        if let pending = json["pending"] as? Bool, pending {
            return "pending"
        }
        return "idle"
    }

    private func runCommand(_ executable: String, _ arguments: [String], timeout: Int) -> Int32 {
        let process = Process()
        process.currentDirectoryURL = projectRoot
        process.executableURL = URL(fileURLWithPath: executable)
        process.arguments = arguments
        process.standardOutput = FileHandle.nullDevice
        process.standardError = FileHandle.nullDevice

        do {
            try process.run()
        } catch {
            lastAction = "Command failed: \(error.localizedDescription)"
            return 127
        }

        let deadline = Date().addingTimeInterval(TimeInterval(timeout))
        while process.isRunning && Date() < deadline {
            Thread.sleep(forTimeInterval: 0.1)
        }
        if process.isRunning {
            process.terminate()
            waitForExit(process, timeout: 1.0)
            if process.isRunning {
                kill(process.processIdentifier, SIGKILL)
            }
            return 124
        }
        return process.terminationStatus
    }

    private func waitForExit(_ process: Process, timeout: TimeInterval) {
        let deadline = Date().addingTimeInterval(timeout)
        while process.isRunning && Date() < deadline {
            Thread.sleep(forTimeInterval: 0.05)
        }
    }

    private func commandOutput(_ executable: String, _ arguments: [String], timeout: Int) -> String? {
        let process = Process()
        let pipe = Pipe()
        process.currentDirectoryURL = projectRoot
        process.executableURL = URL(fileURLWithPath: executable)
        process.arguments = arguments
        process.standardOutput = pipe
        process.standardError = pipe

        do {
            try process.run()
        } catch {
            return nil
        }

        let deadline = Date().addingTimeInterval(TimeInterval(timeout))
        while process.isRunning && Date() < deadline {
            Thread.sleep(forTimeInterval: 0.05)
        }
        if process.isRunning {
            process.terminate()
            return nil
        }

        let data = pipe.fileHandleForReading.readDataToEndOfFile()
        return String(data: data, encoding: .utf8)
    }
}
