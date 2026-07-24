import AppKit
import Darwin
import Foundation

@main
@MainActor
final class AppDelegate: NSObject, NSApplicationDelegate, NSMenuDelegate {
    private let statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
    private let controller = LegoClawdController()
    private var devDirectoryDescriptor: CInt = -1
    private var devDirectorySource: DispatchSourceFileSystemObject?
    private var devDirectoryRefreshWorkItem: DispatchWorkItem?
    private var knownSerialPorts: [String] = []
    private lazy var menuBarImage: NSImage? = loadMenuBarImage()
    private var menuBarUsageText = "5h --%"

    private let bridgeItem = NSMenuItem(title: "Robot Link: stopped", action: nil, keyEquivalent: "")
    private let connectionItem = NSMenuItem(title: "Serial: checking...", action: nil, keyEquivalent: "")
    private let deviceItem = NSMenuItem(title: "Clawd: unknown", action: nil, keyEquivalent: "")
    private let usageFiveHourItem = NSMenuItem(title: "5h --%", action: nil, keyEquivalent: "")
    private let usageWeeklyItem = NSMenuItem(title: "1w --%", action: nil, keyEquivalent: "")
    private let usageResetCreditsItem = NSMenuItem(title: "Resets: unavailable", action: nil, keyEquivalent: "")
    private let usageFiveHourMainLabel = NSTextField(labelWithString: "5h --%")
    private let usageFiveHourResetLabel = NSTextField(labelWithString: "Reset --")
    private let usageWeeklyMainLabel = NSTextField(labelWithString: "1w --%")
    private let usageWeeklyResetLabel = NSTextField(labelWithString: "Reset --")
    private let connectBridgeItem = NSMenuItem(title: "Connect Robot", action: nil, keyEquivalent: "")
    private let disconnectBridgeItem = NSMenuItem(title: "Disconnect Robot", action: nil, keyEquivalent: "")
    private let refreshUsageItem = NSMenuItem(title: "Refresh Usage", action: nil, keyEquivalent: "u")
    private let quietModeItem = NSMenuItem(title: "Quiet Mode", action: nil, keyEquivalent: "m")
    private let testStatesItem = NSMenuItem(title: "State Tests", action: nil, keyEquivalent: "")
    private let testIdleItem = NSMenuItem(title: "Test Idle", action: nil, keyEquivalent: "1")
    private let testWorkingItem = NSMenuItem(title: "Test Working", action: nil, keyEquivalent: "2")
    private let testApprovalItem = NSMenuItem(title: "Test Approval", action: nil, keyEquivalent: "3")
    private let testDoneItem = NSMenuItem(title: "Test Done", action: nil, keyEquivalent: "4")
    private let testErrorItem = NSMenuItem(title: "Test Error", action: nil, keyEquivalent: "5")
    private let testUsageItem = NSMenuItem(title: "Test Show Usage", action: nil, keyEquivalent: "6")
    private let selfTestItem = NSMenuItem(title: "Self-Test", action: nil, keyEquivalent: "s")

    private var testStateItems: [NSMenuItem] {
        [
            testIdleItem,
            testWorkingItem,
            testApprovalItem,
            testDoneItem,
            testErrorItem,
            testUsageItem
        ]
    }

    private var testItems: [NSMenuItem] {
        [testStatesItem] + testStateItems + [selfTestItem]
    }

    static func main() {
        let app = NSApplication.shared
        let delegate = AppDelegate()
        app.delegate = delegate
        app.run()
    }

    func applicationDidFinishLaunching(_ notification: Notification) {
        NSApp.setActivationPolicy(.accessory)
        setupMenu()
        refreshStatus()
        setupDevDirectoryMonitor()
    }

    func applicationWillTerminate(_ notification: Notification) {
        devDirectoryRefreshWorkItem?.cancel()
        devDirectoryRefreshWorkItem = nil
        devDirectorySource?.cancel()
        devDirectorySource = nil
    }

    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool) -> Bool {
        statusItem.button?.performClick(nil)
        return false
    }

    private func setupMenu() {
        statusItem.length = NSStatusItem.variableLength
        if let menuBarImage {
            statusItem.button?.image = menuBarImage
            statusItem.button?.imagePosition = .imageLeading
            statusItem.button?.title = menuBarUsageText
            statusItem.button?.font = NSFont.monospacedDigitSystemFont(
                ofSize: NSFont.smallSystemFontSize,
                weight: .regular
            )
        } else {
            statusItem.button?.title = "Clawd"
        }
        statusItem.button?.toolTip = "Lego Clawd"

        let menu = NSMenu()
        menu.delegate = self
        menu.autoenablesItems = false
        setupUsageViews()
        menu.addItem(groupHeader("Status"))
        menu.addItem(bridgeItem)
        menu.addItem(deviceItem)
        menu.addItem(.separator())

        menu.addItem(groupHeader("Usage"))
        menu.addItem(usageFiveHourItem)
        menu.addItem(usageWeeklyItem)
        menu.addItem(usageResetCreditsItem)
        menu.addItem(refreshUsageItem)
        menu.addItem(.separator())

        menu.addItem(groupHeader("Actions"))
        menu.addItem(connectBridgeItem)
        menu.addItem(disconnectBridgeItem)
        menu.addItem(quietModeItem)
        menu.addItem(.separator())

        menu.addItem(groupHeader("Test"))
        let testStatesMenu = NSMenu()
        testStateItems.forEach { testStatesMenu.addItem($0) }
        testStatesItem.submenu = testStatesMenu
        menu.addItem(testStatesItem)
        menu.addItem(selfTestItem)
        menu.addItem(.separator())

        menu.addItem(NSMenuItem(title: "Quit", action: #selector(quit), keyEquivalent: "q"))

        menu.items.forEach { $0.target = self }
        bridgeItem.isEnabled = true
        bridgeItem.target = self
        bridgeItem.action = #selector(toggleBridge)
        bridgeItem.toolTip = "Toggle robot connection"
        connectBridgeItem.action = #selector(connectBridge)
        disconnectBridgeItem.action = #selector(disconnectBridge)
        refreshUsageItem.action = #selector(refreshUsage)
        quietModeItem.action = #selector(toggleQuietMode)
        testIdleItem.action = #selector(testIdle)
        testWorkingItem.action = #selector(testWorking)
        testApprovalItem.action = #selector(testApproval)
        testDoneItem.action = #selector(testDone)
        testErrorItem.action = #selector(testError)
        testUsageItem.action = #selector(testUsage)
        selfTestItem.action = #selector(selfTest)
        testStateItems.forEach { $0.target = self }

        [connectionItem, deviceItem].forEach { item in
            item.isEnabled = false
            item.target = nil
            item.action = nil
        }
        [usageFiveHourItem, usageWeeklyItem, usageResetCreditsItem].forEach { item in
            item.target = nil
            item.action = nil
        }
        statusItem.menu = menu
    }

    func menuWillOpen(_ menu: NSMenu) {
        refreshStatus()
    }

    private func setupDevDirectoryMonitor() {
        knownSerialPorts = controller.serialPorts()
        let descriptor = open("/dev", O_EVTONLY)
        guard descriptor >= 0 else {
            return
        }
        devDirectoryDescriptor = descriptor
        let source = DispatchSource.makeFileSystemObjectSource(
            fileDescriptor: descriptor,
            eventMask: [.write, .delete, .rename],
            queue: DispatchQueue.main
        )
        source.setEventHandler { [weak self] in
            self?.scheduleConnectionRefresh()
        }
        source.setCancelHandler { [descriptor] in
            close(descriptor)
        }
        source.resume()
        devDirectorySource = source
    }

    private func scheduleConnectionRefresh() {
        guard devDirectoryRefreshWorkItem == nil else {
            return
        }

        let workItem = DispatchWorkItem { [weak self] in
            guard let self else {
                return
            }
            self.devDirectoryRefreshWorkItem = nil
            self.refreshConnectionStatusIfNeeded()
        }
        devDirectoryRefreshWorkItem = workItem
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.25, execute: workItem)
    }

    private func refreshConnectionStatusIfNeeded() {
        let serialPorts = controller.serialPorts()
        guard serialPorts != knownSerialPorts else {
            return
        }
        knownSerialPorts = serialPorts
        refreshConnectionStatus(serialPorts: serialPorts)
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
        if let deviceQuietMode = snapshot.deviceQuietMode {
            controller.quietMode = deviceQuietMode
        }
        menuBarUsageText = snapshot.usageMenuBarText
        applyConnectionStatus(
            isConnected: snapshot.isConnected,
            bridgeRunning: snapshot.bridgeRunning,
            connectionText: snapshot.connectionText
        )
        deviceItem.title = "Clawd: \(clawdStatusText(snapshot))"
        deviceItem.toolTip = "Serial: \(snapshot.connectionText)\nDevice: \(snapshot.deviceDetailText)"
        usageFiveHourItem.isHidden = !snapshot.usageFiveHourAvailable
        usageWeeklyItem.isHidden = !snapshot.usageWeeklyAvailable
        updateUsageWindowView(
            mainLabel: usageFiveHourMainLabel,
            resetLabel: usageFiveHourResetLabel,
            main: snapshot.usageFiveHourText,
            reset: snapshot.usageFiveHourResetText,
            stale: snapshot.usageIsStale
        )
        updateUsageWindowView(
            mainLabel: usageWeeklyMainLabel,
            resetLabel: usageWeeklyResetLabel,
            main: snapshot.usageWeeklyText,
            reset: snapshot.usageWeeklyResetText,
            stale: snapshot.usageIsStale
        )
        updateResetCreditsMenu(
            summary: snapshot.usageResetCreditsText,
            expiryTexts: snapshot.usageResetCreditExpiryTexts,
            stale: snapshot.usageIsStale
        )
        if snapshot.usageNeedsRefresh {
            controller.refreshUsageIfNeeded { [weak self] result in
                Task { @MainActor in
                    if result != 0 {
                        self?.controller.lastAction = "Usage refresh failed: \(result)"
                    }
                    self?.refreshStatus()
                }
            }
        }
    }

    private func refreshConnectionStatus(serialPorts: [String]? = nil) {
        let snapshot = controller.connectionSnapshot(serialPorts: serialPorts)
        applyConnectionStatus(
            isConnected: snapshot.isConnected,
            bridgeRunning: snapshot.bridgeRunning,
            connectionText: snapshot.connectionText
        )
    }

    private func applyConnectionStatus(
        isConnected: Bool,
        bridgeRunning: Bool,
        connectionText: String
    ) {
        if menuBarImage != nil {
            statusItem.button?.title = menuBarUsageText
            statusItem.button?.alphaValue = bridgeRunning ? 1.0 : (isConnected ? 0.65 : 0.35)
        } else {
            statusItem.button?.attributedTitle = fallbackStatusTitle(
                isConnected: isConnected,
                bridgeRunning: bridgeRunning,
                usageText: menuBarUsageText
            )
        }
        bridgeItem.isEnabled = isConnected || bridgeRunning
        connectBridgeItem.isEnabled = isConnected && !bridgeRunning
        disconnectBridgeItem.isEnabled = bridgeRunning
        refreshUsageItem.isEnabled = true
        quietModeItem.isEnabled = isConnected
        quietModeItem.state = controller.quietMode ? .on : .off
        testItems.forEach { $0.isEnabled = isConnected }
        bridgeItem.attributedTitle = menuStatusTitle(
            dot: "●",
            dotColor: bridgeDotColor(isConnected: isConnected, bridgeRunning: bridgeRunning),
            label: "Robot Link",
            value: robotLinkText(isConnected: isConnected, bridgeRunning: bridgeRunning),
            textColor: bridgeItem.isEnabled ? .labelColor : .secondaryLabelColor
        )
        connectionItem.title = "Serial: \(connectionText)"
        deviceItem.title = "Clawd: \(isConnected ? "connected" : "not connected")"
        deviceItem.toolTip = "Serial: \(connectionText)"
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

    private func fallbackStatusTitle(isConnected: Bool, bridgeRunning: Bool, usageText: String) -> NSAttributedString {
        let title = NSMutableAttributedString(string: "Clawd ")
        let dotColor: NSColor
        if bridgeRunning && isConnected {
            dotColor = .systemGreen
        } else if isConnected {
            dotColor = .systemOrange
        } else {
            dotColor = .systemGray
        }
        title.append(NSAttributedString(
            string: "●",
            attributes: [.foregroundColor: dotColor]
        ))
        title.append(NSAttributedString(
            string: " \(usageText)",
            attributes: [
                .foregroundColor: NSColor.labelColor,
                .font: NSFont.monospacedDigitSystemFont(
                    ofSize: NSFont.smallSystemFontSize,
                    weight: .regular
                )
            ]
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

    private func setupUsageViews() {
        usageFiveHourItem.view = usageWindowView(
            mainLabel: usageFiveHourMainLabel,
            resetLabel: usageFiveHourResetLabel
        )
        usageWeeklyItem.view = usageWindowView(
            mainLabel: usageWeeklyMainLabel,
            resetLabel: usageWeeklyResetLabel
        )
    }

    private func usageWindowView(mainLabel: NSTextField, resetLabel: NSTextField) -> NSView {
        configureUsageLabel(
            mainLabel,
            font: NSFont.menuFont(ofSize: NSFont.systemFontSize),
            textColor: .labelColor
        )
        configureUsageLabel(
            resetLabel,
            font: NSFont.menuFont(ofSize: 11),
            textColor: .secondaryLabelColor
        )

        let stack = NSStackView(views: [mainLabel, resetLabel])
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 1
        stack.translatesAutoresizingMaskIntoConstraints = false

        let view = NSView(frame: NSRect(x: 0, y: 0, width: 220, height: 38))
        view.addSubview(stack)
        NSLayoutConstraint.activate([
            stack.leadingAnchor.constraint(equalTo: view.leadingAnchor, constant: 28),
            stack.trailingAnchor.constraint(equalTo: view.trailingAnchor, constant: -16),
            stack.centerYAnchor.constraint(equalTo: view.centerYAnchor)
        ])
        return view
    }

    private func configureUsageLabel(_ label: NSTextField, font: NSFont, textColor: NSColor) {
        label.font = font
        label.textColor = textColor
        label.lineBreakMode = .byTruncatingTail
        label.maximumNumberOfLines = 1
    }

    private func updateUsageWindowView(
        mainLabel: NSTextField,
        resetLabel: NSTextField,
        main: String,
        reset: String,
        stale: Bool
    ) {
        mainLabel.stringValue = main
        resetLabel.stringValue = stale ? "\(reset) · Stale" : reset
    }

    private func updateResetCreditsMenu(summary: String, expiryTexts: [String], stale: Bool) {
        usageResetCreditsItem.title = stale ? "\(summary) · Stale" : summary

        guard !expiryTexts.isEmpty else {
            usageResetCreditsItem.submenu = nil
            return
        }

        let submenu = NSMenu()
        submenu.autoenablesItems = false
        for (index, expiryText) in expiryTexts.enumerated() {
            let item = NSMenuItem(
                title: "\(index + 1). Expires \(expiryText)",
                action: nil,
                keyEquivalent: ""
            )
            item.isEnabled = true
            submenu.addItem(item)
        }
        usageResetCreditsItem.submenu = submenu
    }

    private func clawdStatusText(_ snapshot: StatusSnapshot) -> String {
        guard snapshot.isConnected else {
            return "not connected"
        }

        var parts = [friendlyState(snapshot.aiState)]
        if controller.quietMode {
            parts.append("Quiet")
        }
        parts.append(snapshot.deviceTemperatureText)
        if snapshot.deviceStatusStale {
            parts.append("stale")
        }
        return parts.joined(separator: " · ")
    }

    private func friendlyState(_ state: String) -> String {
        switch state {
        case "idle":
            return "Idle"
        case "working":
            return "Working"
        case "pending":
            return "Pending"
        case "waiting":
            return "Waiting"
        case "error":
            return "Error"
        case "disconnected":
            return "Disconnected"
        case "unknown":
            return "Unknown"
        default:
            return state.prefix(1).uppercased() + state.dropFirst()
        }
    }

    private func robotLinkText(isConnected: Bool, bridgeRunning: Bool) -> String {
        if bridgeRunning {
            return "connected"
        }
        return "not connected"
    }

    private func bridgeDotColor(isConnected: Bool, bridgeRunning: Bool) -> NSColor {
        if bridgeRunning {
            return .systemGreen
        }
        return isConnected ? .systemRed : .systemGray
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
            self.controller.startBridge(quietMode: self.controller.quietMode)
        }
    }

    @objc private func toggleBridge() {
        if disconnectBridgeItem.isEnabled {
            runAction("Disconnect") {
                self.controller.stopBridge(notifyDevice: true)
            }
        } else if connectBridgeItem.isEnabled {
            runAction("Connect") {
                self.controller.startBridge(quietMode: self.controller.quietMode)
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

    @objc private func refreshUsage() {
        controller.lastAction = "Usage refresh: running"
        refreshStatus()
        controller.refreshUsage(force: true) { [weak self] result in
            Task { @MainActor in
                self?.controller.lastAction = result == 0
                    ? "Usage refresh: updated"
                    : "Usage refresh failed: \(result)"
                self?.refreshStatus()
            }
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

    @objc private func testUsage() {
        runAction("Show Usage") {
            self.controller.showUsage()
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
    let deviceTemperatureText: String
    let deviceDetailText: String
    let deviceStatusStale: Bool
    let deviceQuietMode: Bool?
    let usageFiveHourAvailable: Bool
    let usageFiveHourText: String
    let usageFiveHourResetText: String
    let usageMenuBarText: String
    let usageWeeklyAvailable: Bool
    let usageWeeklyText: String
    let usageWeeklyResetText: String
    let usageResetCreditsText: String
    let usageResetCreditExpiryTexts: [String]
    let usageIsStale: Bool
    let usageNeedsRefresh: Bool
    let aiState: String
}

struct ConnectionSnapshot {
    let isConnected: Bool
    let bridgeRunning: Bool
    let connectionText: String
}

struct DeviceStatus {
    let temperatureText: String
    let detailText: String
    let isStale: Bool
    let quietMode: Bool?
}

final class LegoClawdController {
    private static let resetCreditRelativeThreshold: TimeInterval = 24 * 60 * 60
    private static let weeklyMenuBarWarningPercent = 20

    private let projectRoot: URL
    private let bridgeScript: URL
    private let usageBridge: URL
    private let bridgePython: URL
    private let usagePython: URL
    private let bridgeControl: URL
    private var usageRefreshInFlight = false
    private var lastUsageRefreshAttempt: Date?

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
        usageBridge = projectRoot.appendingPathComponent("tools/codex_usage_bridge.py")
        bridgeControl = projectRoot.appendingPathComponent("tools/bridge-control.py")
        bridgePython = FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent(".platformio/penv/bin/python")
        let systemPython = URL(fileURLWithPath: "/usr/bin/python3")
        usagePython = FileManager.default.isExecutableFile(atPath: systemPython.path)
            ? systemPython
            : bridgePython
    }

    func snapshot() -> StatusSnapshot {
        let ports = serialPorts()
        let state = readAIState()
        let deviceStatus = readDeviceStatus()
        let usageStatus = readUsageStatus()
        let bridgeRunning = isBridgeRunning()
        let portText = ports.isEmpty ? "no serial device" : ports.joined(separator: ", ")
        return StatusSnapshot(
            isConnected: !ports.isEmpty,
            bridgeRunning: bridgeRunning,
            connectionText: portText,
            deviceTemperatureText: deviceStatus.temperatureText,
            deviceDetailText: deviceStatus.detailText,
            deviceStatusStale: deviceStatus.isStale,
            deviceQuietMode: deviceStatus.quietMode,
            usageFiveHourAvailable: usageStatus.fiveHourAvailable,
            usageFiveHourText: usageStatus.fiveHourText,
            usageFiveHourResetText: usageStatus.fiveHourResetText,
            usageMenuBarText: usageStatus.menuBarText,
            usageWeeklyAvailable: usageStatus.weeklyAvailable,
            usageWeeklyText: usageStatus.weeklyText,
            usageWeeklyResetText: usageStatus.weeklyResetText,
            usageResetCreditsText: usageStatus.resetCreditsText,
            usageResetCreditExpiryTexts: usageStatus.resetCreditExpiryTexts,
            usageIsStale: usageStatus.isStale,
            usageNeedsRefresh: usageStatus.needsRefresh,
            aiState: state
        )
    }

    func connectionSnapshot(serialPorts: [String]? = nil) -> ConnectionSnapshot {
        let ports = serialPorts ?? self.serialPorts()
        let bridgeRunning = isBridgeRunning()
        return ConnectionSnapshot(
            isConnected: !ports.isEmpty,
            bridgeRunning: bridgeRunning,
            connectionText: ports.isEmpty ? "no serial device" : ports.joined(separator: ", ")
        )
    }

    func refreshUsageIfNeeded(completion: @escaping (Int32) -> Void) {
        refreshUsage(force: false, completion: completion)
    }

    func refreshUsage(force: Bool, completion: @escaping (Int32) -> Void) {
        guard !usageRefreshInFlight else {
            return
        }
        if !force,
           let lastUsageRefreshAttempt,
           Date().timeIntervalSince(lastUsageRefreshAttempt) < 300 {
            return
        }

        usageRefreshInFlight = true
        lastUsageRefreshAttempt = Date()
        let executable = usagePython.path
        let script = usageBridge.path
        let usageState = projectRoot.appendingPathComponent(".lego-clawd/usage-state.json").path
        let logFile = projectRoot.appendingPathComponent(".lego-clawd/bridge.log").path
        DispatchQueue.global(qos: .utility).async {
            let result = self.runStatusCommand(
                executable,
                [
                    script,
                    "--dry-run",
                    "--once",
                    "--state",
                    "idle",
                    "--usage-source",
                    "auto",
                    "--usage-state-file",
                    usageState,
                    "--log-file",
                    logFile
                ],
                timeout: 20
            )
            DispatchQueue.main.async {
                self.usageRefreshInFlight = false
                completion(result)
            }
        }
    }

    func startBridge(quietMode: Bool) {
        let result = runCommand(
            bridgePython.path,
            [bridgeControl.path, "start", "--quiet-mode", quietMode ? "true" : "false"],
            timeout: 8
        )
        if result != 0 {
            lastAction = "Connect failed: \(result)"
            return
        }
        lastAction = isBridgeRunning() ? "Connect: robot connected" : "Connect failed"
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
        runWithBridgePaused(label: "State \(state)", resumeDelay: 5.0) {
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
        runWithBridgePaused(label: "Self-test", resumeDelay: 35.0) {
            _ = self.runCommand(
                self.bridgeScript.path,
                ["--once", "--self-test"],
                timeout: 20
            )
        }
    }

    func showUsage() {
        runWithBridgePaused(label: "Show usage", resumeDelay: 10.0) {
            _ = self.runCommand(
                self.bridgeScript.path,
                ["--once", "--show-usage"],
                timeout: 15
            )
        }
    }

    private func runWithBridgePaused(label: String, resumeDelay: TimeInterval = 0, action: () -> Void) {
        let wasRunning = isBridgeRunning()
        if wasRunning {
            stopBridge(notifyDevice: false)
        }
        action()
        lastAction = "\(label): sent"
        if wasRunning && resumeDelay > 0 {
            Thread.sleep(forTimeInterval: resumeDelay)
        }
        if wasRunning {
            startBridge(quietMode: quietMode)
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

    func serialPorts() -> [String] {
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

    private func readDeviceStatus() -> DeviceStatus {
        let url = projectRoot.appendingPathComponent(".lego-clawd/device-status.json")
        guard let data = try? Data(contentsOf: url),
              let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else {
            return DeviceStatus(
                temperatureText: "-- C",
                detailText: "unknown",
                isStale: true,
                quietMode: nil
            )
        }

        var parts: [String] = []
        var temperatureText = "-- C"
        if let temp = json["temperatureC"] as? Double {
            temperatureText = String(format: "%.1f C", temp)
            parts.append("temperature \(temperatureText)")
        } else if let temp = json["temperatureC"] as? Int {
            temperatureText = "\(temp) C"
            parts.append("temperature \(temperatureText)")
        } else {
            parts.append("temperature -- C")
        }

        if let lcd = json["lcd"] as? String {
            if let backlight = json["backlightPercent"] as? Int {
                parts.append("LCD \(lcd) \(backlight)%")
            } else {
                parts.append("LCD \(lcd)")
            }
        }

        let quietMode: Bool?
        if let quiet = json["quietMode"] as? Bool {
            quietMode = quiet
            parts.append("quiet \(quiet ? "on" : "off")")
        } else if let quiet = json["quiet"] as? Bool {
            quietMode = quiet
            parts.append("quiet \(quiet ? "on" : "off")")
        } else {
            quietMode = nil
        }

        var isStale = false
        if let updatedAt = json["updatedAt"] as? String,
           let date = ISO8601DateFormatter().date(from: updatedAt),
           Date().timeIntervalSince(date) > 45 {
            isStale = true
            parts.append("stale")
        }

        return DeviceStatus(
            temperatureText: temperatureText,
            detailText: parts.joined(separator: " | "),
            isStale: isStale,
            quietMode: quietMode
        )
    }

    private struct UsageMenuStatus {
        let fiveHourAvailable: Bool
        let fiveHourText: String
        let fiveHourResetText: String
        let menuBarText: String
        let weeklyAvailable: Bool
        let weeklyText: String
        let weeklyResetText: String
        let resetCreditsText: String
        let resetCreditExpiryTexts: [String]
        let isStale: Bool
        let needsRefresh: Bool
    }

    private func readUsageStatus() -> UsageMenuStatus {
        let url = projectRoot.appendingPathComponent(".lego-clawd/usage-state.json")
        guard let data = try? Data(contentsOf: url),
              let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else {
            return UsageMenuStatus(
                fiveHourAvailable: false,
                fiveHourText: "5h --%",
                fiveHourResetText: "Reset --",
                menuBarText: "Usage --",
                weeklyAvailable: false,
                weeklyText: "1w --%",
                weeklyResetText: "Reset --",
                resetCreditsText: "Resets: unavailable",
                resetCreditExpiryTexts: [],
                isStale: true,
                needsRefresh: true
            )
        }

        let fiveHourValue = json["fiveHour"]
        let weeklyValue = json["weekly"]
        let fiveHour = usageWindowText(fiveHourValue, label: "5h", resetStyle: .time)
        let weekly = usageWindowText(weeklyValue, label: "1w", resetStyle: .date)
        let resetCredits = resetCreditsDisplay(json["resetCredits"])
        var isStale = false

        if let updatedAt = json["updatedAt"] as? String,
           let date = ISO8601DateFormatter().date(from: updatedAt),
           Date().timeIntervalSince(date) > 420 {
            isStale = true
        }

        if let stale = json["stale"] as? Bool, stale {
            isStale = true
        }

        return UsageMenuStatus(
            fiveHourAvailable: fiveHourValue is [String: Any],
            fiveHourText: fiveHour.main,
            fiveHourResetText: fiveHour.reset,
            menuBarText: usageMenuBarText(fiveHourValue: fiveHourValue, weeklyValue: weeklyValue),
            weeklyAvailable: weeklyValue is [String: Any],
            weeklyText: weekly.main,
            weeklyResetText: weekly.reset,
            resetCreditsText: resetCredits.summary,
            resetCreditExpiryTexts: resetCredits.expiryTexts,
            isStale: isStale,
            needsRefresh: isStale
        )
    }

    private enum UsageResetStyle {
        case time
        case date
    }

    private func usageWindowText(_ value: Any?, label: String, resetStyle: UsageResetStyle) -> (main: String, reset: String) {
        guard let window = value as? [String: Any] else {
            return ("\(label) --%", "Reset --")
        }

        let remaining = intValue(window["remainingPercent"]).map { "\($0)%" } ?? "--%"
        let reset = resetText(window["resetAt"], style: resetStyle)
        return ("\(label) \(remaining)", "Reset \(reset)")
    }

    private func usageMenuBarText(fiveHourValue: Any?, weeklyValue: Any?) -> String {
        let fiveHourRemaining = usageRemainingPercent(fiveHourValue)
        let weeklyRemaining = usageRemainingPercent(weeklyValue)

        if let weeklyRemaining,
           weeklyRemaining < Self.weeklyMenuBarWarningPercent {
            return "1w \(weeklyRemaining)%"
        }

        if let fiveHourRemaining {
            return "5h \(fiveHourRemaining)%"
        }

        if let weeklyRemaining {
            return "1w \(weeklyRemaining)%"
        }

        return "Usage --"
    }

    private func usageRemainingPercent(_ value: Any?) -> Int? {
        guard let window = value as? [String: Any] else {
            return nil
        }
        return intValue(window["remainingPercent"])
    }

    private func resetCreditsDisplay(_ value: Any?) -> (summary: String, expiryTexts: [String]) {
        guard let credits = value as? [String: Any] else {
            return ("Resets: unavailable", [])
        }

        let summary: String
        if let count = intValue(credits["availableCount"]) {
            summary = "Resets: \(count) available"
        } else {
            summary = "Resets: unavailable"
        }

        guard let details = credits["credits"] as? [[String: Any]] else {
            return (summary, [])
        }

        let expiryDates = details.compactMap { credit -> Date? in
            if let status = credit["status"] as? String, status != "available" {
                return nil
            }
            guard let expiresAt = credit["expiresAt"] as? String else {
                return nil
            }
            return ISO8601DateFormatter().date(from: expiresAt)
        }.sorted()

        let now = Date()
        return (summary, expiryDates.map { resetCreditExpiryText($0, relativeTo: now) })
    }

    private func resetCreditExpiryText(_ date: Date, relativeTo now: Date) -> String {
        let formatter = DateFormatter()
        formatter.locale = Locale(identifier: "en_US_POSIX")
        formatter.dateFormat = "MMM d, h:mm:ss a"
        let absoluteText = formatter.string(from: date)

        let remaining = date.timeIntervalSince(now)
        if remaining <= 0 {
            return "now · \(absoluteText)"
        }
        if remaining <= Self.resetCreditRelativeThreshold {
            let totalMinutes = max(1, Int(remaining / 60))
            let hours = totalMinutes / 60
            let minutes = totalMinutes % 60
            if hours == 0 {
                return "in \(minutes)m · \(absoluteText)"
            }
            return "in \(hours)h \(minutes)m · \(absoluteText)"
        }
        return absoluteText
    }

    private func resetText(_ value: Any?, style: UsageResetStyle) -> String {
        guard let text = value as? String, !text.isEmpty else {
            return "--"
        }
        guard let date = ISO8601DateFormatter().date(from: text) else {
            return text
        }

        let formatter = DateFormatter()
        formatter.locale = Locale(identifier: "en_US_POSIX")
        switch style {
        case .time:
            formatter.dateFormat = "h:mm a"
        case .date:
            formatter.dateFormat = "MMM d"
        }
        return formatter.string(from: date)
    }

    private func intValue(_ value: Any?) -> Int? {
        if let int = value as? Int {
            return int
        }
        if let double = value as? Double {
            return Int(double.rounded())
        }
        return nil
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

    private func runStatusCommand(_ executable: String, _ arguments: [String], timeout: Int) -> Int32 {
        let process = Process()
        process.currentDirectoryURL = projectRoot
        process.executableURL = URL(fileURLWithPath: executable)
        process.arguments = arguments
        process.standardOutput = FileHandle.nullDevice
        process.standardError = FileHandle.nullDevice

        do {
            try process.run()
        } catch {
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
