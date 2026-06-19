import AppKit
import Darwin
import Foundation

@main
@MainActor
final class AppDelegate: NSObject, NSApplicationDelegate {
    private let statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
    private let controller = LegoClawdController()
    private var refreshTimer: Timer?
    private lazy var menuBarImage: NSImage? = loadMenuBarImage()

    private let bridgeItem = NSMenuItem(title: "Bridge: stopped", action: nil, keyEquivalent: "")
    private let connectionItem = NSMenuItem(title: "Serial: checking...", action: nil, keyEquivalent: "")
    private let aiStateItem = NSMenuItem(title: "State: unknown", action: nil, keyEquivalent: "")
    private let lastActionItem = NSMenuItem(title: "Last: none", action: nil, keyEquivalent: "")

    static func main() {
        let app = NSApplication.shared
        let delegate = AppDelegate()
        app.delegate = delegate
        app.setActivationPolicy(.accessory)
        app.run()
    }

    func applicationDidFinishLaunching(_ notification: Notification) {
        setupMenu()
        refreshStatus()
        refreshTimer = Timer.scheduledTimer(withTimeInterval: 2.0, repeats: true) { [weak self] _ in
            Task { @MainActor in
                self?.refreshStatus()
            }
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
        menu.autoenablesItems = false
        menu.addItem(groupHeader("Status"))
        menu.addItem(bridgeItem)
        menu.addItem(connectionItem)
        menu.addItem(aiStateItem)
        menu.addItem(lastActionItem)
        menu.addItem(.separator())

        menu.addItem(groupHeader("Actions"))
        menu.addItem(NSMenuItem(title: "Connect Bridge", action: #selector(connectBridge), keyEquivalent: "c"))
        menu.addItem(NSMenuItem(title: "Disconnect Bridge", action: #selector(disconnectBridge), keyEquivalent: "d"))
        menu.addItem(.separator())

        menu.addItem(groupHeader("Test"))
        menu.addItem(NSMenuItem(title: "Test Idle", action: #selector(testIdle), keyEquivalent: "1"))
        menu.addItem(NSMenuItem(title: "Test Working", action: #selector(testWorking), keyEquivalent: "2"))
        menu.addItem(NSMenuItem(title: "Test Approval", action: #selector(testApproval), keyEquivalent: "3"))
        menu.addItem(NSMenuItem(title: "Test Done", action: #selector(testDone), keyEquivalent: "4"))
        menu.addItem(NSMenuItem(title: "Approval Test 10s", action: #selector(approvalTest), keyEquivalent: "a"))
        menu.addItem(NSMenuItem(title: "Self-Test", action: #selector(selfTest), keyEquivalent: "s"))
        menu.addItem(.separator())

        menu.addItem(NSMenuItem(title: "Quit", action: #selector(quit), keyEquivalent: "q"))

        menu.items.forEach { $0.target = self }
        bridgeItem.isEnabled = true
        bridgeItem.target = self
        bridgeItem.action = #selector(toggleBridge)
        bridgeItem.toolTip = "Toggle bridge connection"

        [connectionItem, aiStateItem, lastActionItem].forEach { item in
            item.isEnabled = true
            item.target = nil
            item.action = nil
        }
        statusItem.menu = menu
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
        bridgeItem.attributedTitle = menuStatusTitle(
            dot: snapshot.bridgeRunning ? "●" : "●",
            dotColor: snapshot.bridgeRunning ? .systemGreen : .systemRed,
            label: "Bridge",
            value: snapshot.bridgeText
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
            dotColor = .systemRed
        }
        title.append(NSAttributedString(
            string: "●",
            attributes: [.foregroundColor: dotColor]
        ))
        return title
    }

    private func menuStatusTitle(dot: String, dotColor: NSColor, label: String, value: String) -> NSAttributedString {
        let result = NSMutableAttributedString()
        let textAttributes: [NSAttributedString.Key: Any] = [
            .foregroundColor: NSColor.labelColor,
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
        let shouldDisconnect = controller.snapshot().bridgeRunning
        runAction(shouldDisconnect ? "Disconnect" : "Connect") {
            if shouldDisconnect {
                self.controller.stopBridge()
            } else {
                self.controller.startBridge()
            }
        }
    }

    @objc private func disconnectBridge() {
        runAction("Disconnect") {
            self.controller.stopBridge()
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
            self.controller.sendState("pending")
        }
    }

    @objc private func testDone() {
        runAction("Done") {
            self.controller.sendState("waiting")
        }
    }

    @objc private func approvalTest() {
        runAction("Approval Test") {
            self.controller.approvalTest(seconds: 10)
        }
    }

    @objc private func selfTest() {
        runAction("Self-Test") {
            self.controller.selfTest()
        }
    }

    @objc private func quit() {
        controller.stopBridge()
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
        let portText = ports.isEmpty ? "disconnected" : ports.joined(separator: ", ")
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
        let result = runCommand(bridgePython.path, [bridgeControl.path, "start"], timeout: 8)
        if result != 0 {
            lastAction = "Connect failed: bridge-control \(result)"
            return
        }
        lastAction = isBridgeRunning() ? "Connect: bridge started" : "Connect failed: bridge exited"
    }

    func stopBridge() {
        _ = runCommand(bridgePython.path, [bridgeControl.path, "stop"], timeout: 8)
        lastAction = "Disconnect: serial released"
    }

    func sendState(_ state: String) {
        runWithBridgePaused(label: "State \(state)") {
            _ = self.runCommand(self.bridgeScript.path, ["--once", "--state", state], timeout: 15)
        }
    }

    func approvalTest(seconds: Int) {
        runWithBridgePaused(label: "Approval test") {
            _ = self.runCommand(self.bridgeScript.path, ["--approval-test", "\(seconds)"], timeout: seconds + 15)
        }
    }

    func selfTest() {
        runWithBridgePaused(label: "Self-test") {
            _ = self.runCommand(self.bridgeScript.path, ["--once", "--self-test"], timeout: 20)
        }
    }

    private func runWithBridgePaused(label: String, action: () -> Void) {
        let wasRunning = isBridgeRunning()
        if wasRunning {
            stopBridge()
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
