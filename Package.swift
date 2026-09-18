// swift-tools-version: 6.0
import PackageDescription

let package = Package(
	name: "Pippin",
	platforms: [
		.macOS(.v12),
		.iOS(.v15),
	],
	products: [
		.library(name: "Pippin", targets: ["Pippin"]),
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
			// boot/core.clj and boot/clojure/*.clj reach the binary through the generated .inc files (make boot);
			// boot/*.c are the compiled units, empty unless CLJ_COMPILED_CORE is defined.
			exclude: ["boot/core.clj", "boot/clojure", "core_clj.inc", "libs_clj.inc"],
			cSettings: [
				.headerSearchPath("."),
				// unsafeFlags makes the package unusable as a dependency; fine while it is a root package.
				.unsafeFlags(["-Wall", "-Wextra", "-Wpedantic", "-Werror"]),
				.unsafeFlags(["-DCLJ_DEBUG=1"], .when(configuration: .debug)),
			]
		),
		// The C generator over the analyzer's trees; the compiled-eval hook links into the tests and the bench.
		.target(
			name: "CljCompiler",
			dependencies: ["CljCore"],
			cSettings: [
				.headerSearchPath("../CljCore"),
				.unsafeFlags(["-Wall", "-Wextra", "-Wpedantic", "-Werror"]),
				.unsafeFlags(["-DCLJ_DEBUG=1"], .when(configuration: .debug)),
			]
		),
		.executableTarget(
			name: "clj-compile",
			dependencies: ["CljCore", "CljCompiler", "Pippin"]
		),
		.target(
			name: "Pippin",
			dependencies: ["CljCore"]
		),
		.testTarget(
			name: "PippinTests",
			dependencies: ["Pippin", "CljCore", "CljCompiler"],
			exclude: ["Fixtures"]
		),
		// The type-coverage metric over core.clj, the embedded libs and the corpus (docs/facts-coverage.md).
		.executableTarget(
			name: "clj-facts",
			dependencies: ["CljCore", "Pippin"],
			cSettings: [
				.unsafeFlags(["-Wall", "-Wextra", "-Wpedantic", "-Werror"]),
			]
		),
		.executableTarget(
			name: "clj-api-dump",
			dependencies: ["CljCore", "Pippin"]
		),
		.executableTarget(
			name: "clj-bench",
			dependencies: [
				"CljCore",
				"CljCompiler",
				"Pippin",
				.product(name: "HashTreeCollections", package: "swift-collections"),
			]
		),
	],
	cLanguageStandard: .c17
)
