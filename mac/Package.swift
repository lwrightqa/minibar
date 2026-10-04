// swift-tools-version:6.0
//
// TinyBar for Mac: a menu-bar app that shows "On a call" on a TinyBar when the
// Mac's microphone or camera is in use. See README.md.
//
// Targets:
// - TinyBarCore: plain Swift (Foundation only). Builds and is tested on Linux
//   and macOS. Call detection logic, the API messages (docs/api.md), USB serial
//   and HTTP transports, pairing, settings, and what the menu says.
// - TinyBarMac: the macOS app (AppKit, SwiftUI, CoreAudio, CoreMediaIO, IOKit,
//   Network, Security, ServiceManagement). Every file is wrapped in
//   `#if os(macOS)`, so on Linux this target compiles to a stub that only
//   prints a message, and the macOS code is syntax-checked but not type-checked.
// - TinyBarCoreTests: XCTest tests of TinyBarCore.

import PackageDescription

let package = Package(
    name: "TinyBarMac",
    // macOS 14 is the minimum because the per-process microphone list
    // (kAudioHardwarePropertyProcessObjectList) is new in macOS 14
    // (docs/mac-app.md, "Signals"). Ignored when building on Linux.
    platforms: [.macOS(.v14)],
    products: [
        .library(name: "TinyBarCore", targets: ["TinyBarCore"]),
        // The app's executable. scripts/build-app.sh wraps it in TinyBar.app.
        .executable(name: "TinyBar", targets: ["TinyBarMac"]),
    ],
    targets: [
        .target(name: "TinyBarCore"),
        .executableTarget(
            name: "TinyBarMac",
            dependencies: ["TinyBarCore"]
        ),
        .testTarget(
            name: "TinyBarCoreTests",
            dependencies: ["TinyBarCore"]
        ),
    ],
    swiftLanguageModes: [.v6]
)
