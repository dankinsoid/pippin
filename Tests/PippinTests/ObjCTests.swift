// @ai-generated(solo)
import CljCore
import Testing
@testable import Pippin

extension CoreTests {
	@Suite struct ObjCTests {
		let rt = Runtime()

		@Test func kebabSpelling() throws {
			#expect(try rt.eval(#"(objc-kebab* "addTarget:action:forControlEvents:")"#) == "add-target:action:for-control-events:")
			#expect(try rt.eval(#"(objc-kebab* "UTF8String")"#) == "utf8-string")
			#expect(try rt.eval(#"(objc-kebab* "centerXAnchor")"#) == "center-x-anchor")
			#expect(try rt.eval(#"(objc-kebab* "initWithFrame:")"#) == "init-with-frame:")
			#expect(try rt.eval(#"(objc-kebab* "length")"#) == "length")
			#expect(try rt.eval(#"(objc-kebab* "setNeedsDisplayInRect:")"#) == "set-needs-display-in-rect:")
			#expect(try rt.eval(#"(objc-kebab* "URLWithString:")"#) == "url-with-string:")
		}

		@Test func classesResolve() throws {
			#expect(try rt.eval(#"(objc-object? (objc-class "NSString"))"#) == true)
			#expect(try rt.eval(#"(objc-class "NoSuchClassHere")"#) == nil)
			#expect(try rt.eval("(objc-object? 1)") == false)
		}

		// An NSString return crosses as a value; an NSMutableString stays a wrapper, so it can be mutated.
		@Test func stringsCrossAsValues() throws {
			#expect(try rt.eval(#"(objc-send (objc-class "NSString") "stringWithUTF8String:" "hi")"#) == "hi")
			let mutable = #"(objc-send (objc-class "NSMutableString") "stringWithUTF8String:" "ab")"#
			#expect(try rt.eval("(objc-object? \(mutable))") == true)
			#expect(try rt.eval("(objc-send \(mutable) \"length\")") == 2)
			#expect(try rt.eval("""
			(let [s \(mutable)]
			  (objc-send s "appendString:" "cd")
			  (objc-send s "UTF8String"))
			""") == "abcd")
		}

		@Test func numbersCrossAsValues() throws {
			#expect(try rt.eval(#"(objc-send (objc-class "NSNumber") "numberWithLongLong:" 7)"#) == 7)
			#expect(try rt.eval(#"(objc-send (objc-class "NSNumber") "numberWithDouble:" 1.5)"#) == 1.5)
			#expect(try rt.eval(#"(objc-send (objc-class "NSNumber") "numberWithBool:" true)"#) == true)
			#expect(try rt.eval(#"(objc-send (objc-class "NSNumber") "numberWithBool:" false)"#) == false)
		}

		// -[NSDate initWithTimeInterval:sinceDate:] takes a double then a pointer: proof that the two
		// register files are filled independently and in each one's own order.
		@Test func mixedRegisterClasses() throws {
			#expect(try rt.eval("""
			(let [epoch (objc-send (objc-class "NSDate") "dateWithTimeIntervalSince1970:" 0.0)
			      d (objc-send (objc-send (objc-class "NSDate") "alloc") "initWithTimeInterval:sinceDate:" 1.5 epoch)]
			  (objc-send d "timeIntervalSince1970"))
			""") == 1.5)
			#expect(try rt.eval(#"(objc-send (objc-class "NSNumber") "numberWithFloat:" 0.5)"#) == 0.5)
		}

		@Test func boolAndVoidReturns() throws {
			#expect(try rt.eval(#"(objc-send (objc-class "NSString") "isSubclassOfClass:" (objc-class "NSObject"))"#) == true)
			#expect(try rt.eval(#"(objc-send (objc-class "NSString") "isSubclassOfClass:" (objc-class "NSNumber"))"#) == false)
		}

		// Messaging nil is a no-op in Objective-C and stays one here.
		@Test func nilTarget() throws {
			#expect(try rt.eval(#"(objc-send nil "length")"#) == nil)
		}

		@Test func errorsExplain() throws {
			let unknown = cljEvalError(#"(objc-send (objc-class "NSString") "noSuchSelector")"#)
			#expect(unknown?.contains("No selector noSuchSelector") == true)
			let hinted = cljEvalError(#"(objc-send (objc-class "NSString") "stringWithUTF8String")"#)
			#expect(hinted?.contains("stringWithUTF8String:") == true)
			#expect(cljEvalError(#"(objc-send 1 "length")"#)?.contains("not an Objective-C object") == true)
			let arity = cljEvalError(#"(objc-send (objc-class "NSString") "stringWithUTF8String:")"#)
			#expect(arity?.contains("takes 1 argument(s), got 0") == true)
			let badArg = cljEvalError(#"(objc-send (objc-class "NSString") "stringWithUTF8String:" :kw)"#)
			#expect(badArg?.contains("does not convert") == true)
		}

		// alloc/init hand back +1, so the wrapper must not retain again; the object survives its Clojure value.
		@Test func allocInitOwnership() throws {
			#expect(try rt.eval("""
			(let [s (objc-send (objc-send (objc-class "NSMutableString") "alloc") "init")]
			  (objc-send s "appendString:" "ok")
			  [(objc-send s "retainCount") (objc-send s "UTF8String")])
			""") == [1, "ok"])
		}

		@Test func wrappersAreIdentity() throws {
			#expect(try rt.eval(#"(= (objc-class "NSString") (objc-class "NSString"))"#) == true)
			#expect(try rt.eval(#"(= (objc-class "NSString") (objc-class "NSNumber"))"#) == false)
		}

		// (.base target arg :label arg …): labels are syntax, arguments stay positional (design §5).
		@Test func methodForm() throws {
			#expect(try rt.eval(#"(.string-with-utf8-string (objc-class "NSString") "hi")"#) == "hi")
			#expect(try rt.eval(#"(.length (.string-with-utf8-string (objc-class "NSMutableString") "abc"))"#) == 3)
			#expect(try rt.eval("""
			(let [epoch (.date-with-time-interval-since1970 (objc-class "NSDate") 0.0)
			      d (.init-with-time-interval (.alloc (objc-class "NSDate")) 1.5 :since-date epoch)]
			  (.time-interval-since1970 d))
			""") == 1.5)
			#expect(try rt.eval(#"(.length nil)"#) == nil)
		}

		@Test func methodFormIsCheckedAtAnalysis() throws {
			#expect(cljEvalError("(.length)")?.contains("needs a target") == true)
			#expect(cljEvalError(#"(.foo (objc-class "NSString") 1 "bar" 2)"#)?.contains("literal unqualified keyword labels") == true)
			#expect(cljEvalError(#"(.foo (objc-class "NSString") 1 :bar)"#)?.contains("has no argument") == true)
			// A label out of order names another selector, and the message shows the ones that exist.
			let reordered = cljEvalError(#"(.init-with-time-interval (.alloc (objc-class "NSDate")) 1.5 :since-dates 2)"#)
			#expect(reordered?.contains("init-with-time-interval:since-date:") == true)
		}

		// A send from a coroutine leaves its pool for clj_coro_switch_out to drain, across a park.
		@Test func sendsFromACoroutine() throws {
			#expect(try cljEvalScoped("""
			(ns objc-go-tests (:require [clojure.core.async :refer [chan <! >! <!! timeout go]]))
			(let [c (chan)]
			  (go (>! c (objc-send (objc-class "NSString") "stringWithUTF8String:" "from-go")))
			  (<!! c))
			""") == "from-go")
			#expect(try cljEvalScoped("""
			(in-ns 'objc-go-tests)
			(let [c (chan)]
			  (go (let [s (objc-send (objc-class "NSMutableString") "stringWithUTF8String:" "a")]
			        (<! (timeout 1))
			        (objc-send s "appendString:" "b")
			        (>! c (objc-send s "UTF8String"))))
			  (<!! c))
			""") == "ab")
		}
	}
}
