// swift-tools-version: 6.0

import PackageDescription

let package = Package(
    name: "BlackHole",
    platforms: [.macOS(.v14)],
    products: [
        .executable(name: "BlackHoleApp", targets: ["BlackHoleApp"]),
        .library(name: "BlackHoleCore", targets: ["BlackHoleCore"]),
    ],
    targets: [
        .target(name: "BlackHoleCore"),
        .executableTarget(
            name: "BlackHoleApp",
            dependencies: ["BlackHoleCore"],
            resources: [.copy("Shaders")],
            linkerSettings: [
                .linkedFramework("AppKit"),
                .linkedFramework("Metal"),
                .linkedFramework("MetalKit"),
                .linkedFramework("MetalFX"),
                .linkedFramework("QuartzCore"),
            ]
        ),
        .testTarget(name: "BlackHoleCoreTests", dependencies: ["BlackHoleCore"]),
    ],
    swiftLanguageModes: [.v5]
)
