// swift-tools-version: 5.9
// The swift-tools-version declares the minimum version of Swift required to build this package.

import PackageDescription

let package = Package(
    name: "ClojureSwift",
    platforms: [
        .macOS(.v13),
        .iOS(.v16)
    ],
    products: [
        .library(
            name: "ClojureSwift",
            targets: ["ClojureSwift"]),
    ],
    targets: [
        .target(
            name: "ClojureSwift"),
        .testTarget(
            name: "ClojureSwiftTests",
            dependencies: ["ClojureSwift"]),
    ]
)
