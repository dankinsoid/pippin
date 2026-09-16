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
	dependencies: [
		// Bench-only: TreeDictionary is the reference persistent map on Swift classes.
		.package(url: "https://github.com/apple/swift-collections.git", from: "1.1.0"),
	],
	targets: [
		// Portable runtime core. No platform headers here except os/signpost.h under __APPLE__ (profile.c);
		// everything else host-specific goes through the Swift target.
		.target(
			name: "CljCore",
			// boot/core.clj and boot/clojure/*.clj reach the binary through the generated .inc files (make boot).
			exclude: ["boot", "core_clj.inc", "libs_clj.inc"],
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
		.executableTarget(
			name: "clj-api-dump",
			dependencies: ["CljCore", "Clojure"]
		),
		.executableTarget(
			name: "clj-bench",
			dependencies: [
				"CljCore",
				"Clojure",
				.product(name: "HashTreeCollections", package: "swift-collections"),
			]
		),
	],
	cLanguageStandard: .c17
)
