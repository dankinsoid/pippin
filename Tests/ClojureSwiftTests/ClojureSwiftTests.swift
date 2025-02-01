import XCTest
@testable import ClojureSwift

final class ClojureSwiftTests: XCTestCase {
    func testBasicEvaluation() throws {
        let interpreter = ClojureSwift()
        let result = try interpreter.evaluate("(+ 1 2)")
        XCTAssertEqual(result as? String, "(+ 1 2)") // This will need to be updated once we implement actual evaluation
    }
}
