// swift-tools-version: 6.0

import PackageDescription

let package = Package(
    name: "LegoClawdBar",
    platforms: [
        .macOS(.v14)
    ],
    products: [
        .executable(name: "LegoClawdBar", targets: ["LegoClawdBar"])
    ],
    targets: [
        .executableTarget(
            name: "LegoClawdBar",
            resources: [
                .copy("Resources")
            ],
            linkerSettings: [
                .linkedFramework("AppKit")
            ]
        )
    ],
    swiftLanguageModes: [.v5]
)
