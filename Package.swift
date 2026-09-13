// swift-tools-version: 6.0
import PackageDescription

let package = Package(
	name: "Clojure",
	platforms: [
		.macOS(.v14),
		.iOS(.v17),
	],
	products: [
		.library(name: "Clojure", targets: ["Clojure"]),
	],
	targets: [
		// Portable runtime core. No platform headers here; everything host-specific goes through the Swift target.
		.target(
			name: "CljCore",
			cSettings: [
				// unsafeFlags makes the package unusable as a dependency; fine while it is a root package.
				.unsafeFlags(["-Wall", "-Wextra", "-Wpedantic", "-Werror"]),
				.unsafeFlags(["-DCLJ_DEBUG=1"], .when(configuration: .debug)),
			]
		),
		.target(
			name: "Clojure",
			dependencies: ["CljCore"]
		),
		.testTarget(
			name: "ClojureTests",
			dependencies: ["Clojure", "CljCore"]
		),
	],
	cLanguageStandard: .c17
)
