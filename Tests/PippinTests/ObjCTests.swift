// @ai-generated(solo)
import CljCore
import Foundation
import Testing
@testable import Pippin

extension CoreTests {
	@Suite struct ObjCTests {
		let rt = Runtime()

		// A send allocates nothing that outlives its result: the wrapper owns the +1 and drops it.
		@Test func sendsLeakNothing() throws {
			_ = try cljEval(#"(.utf8-string (ns-mutable-string "ab"))"#)
			let before = clj_debug_live_objects()
			for _ in 0 ..< 8 {
				_ = try cljEval(#"(.utf8-string (ns-mutable-string "ab"))"#)
			}
			#expect(clj_debug_live_objects() == before)
		}

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

		// Every NSString crosses as a value, whatever its class: __NSCFString is registered under
		// NSMutableString either way, so a mutability test would only split them by length.
		@Test func stringsCrossAsValues() throws {
			#expect(try rt.eval(#"(objc-send (objc-class "NSString") "stringWithUTF8String:" "hi")"#) == "hi")
			// Longer than a tagged pointer holds: the class is __NSCFString and the old test called it mutable.
			#expect(try rt.eval(#"(objc-send (objc-class "NSString") "stringWithUTF8String:" "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")"#)
				== "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")
			#expect(try rt.eval(#"(objc-send (objc-class "NSMutableString") "stringWithUTF8String:" "ab")"#) == "ab")
		}

		// The object itself by explicit request, the gesture ns-array makes for a vector (design §5).
		@Test func handlesAreAskedForByName() throws {
			#expect(try rt.eval(#"(objc-object? (ns-string "hi"))"#) == true)
			#expect(try rt.eval(#"(ns-string->str (ns-string "hi"))"#) == "hi")
			#expect(try rt.eval(#"(.length (ns-string "abc"))"#) == 3)
			#expect(try rt.eval("""
			(let [s (ns-mutable-string "ab")]
			  (.append-string s "cd")
			  [(ns-string->str s) (.length s)])
			""") == ["abcd", 4])
			#expect(try rt.eval(#"(ns-string->str nil)"#) == nil)
			#expect(cljEvalError(#"(ns-string 1)"#)?.contains("expects a string") == true)
			#expect(cljEvalError(#"(ns-string->str (ns-array []))"#)?.contains("expects an NSString") == true)
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
			(let [s (ns-mutable-string "")]
			  (objc-send s "appendString:" "ok")
			  [(objc-send s "retainCount") (objc-send s "UTF8String")])
			""") == [1, "ok"])
		}

		// An allocation is not an object of its class yet: -[NSPlaceholderMutableString length] raises, so
		// alloc stays a handle where every other NSString return is read.
		@Test func anAllocationIsNotConverted() throws {
			#expect(try rt.eval(#"(objc-object? (.alloc (objc-class "NSMutableString")))"#) == true)
			// What -init answers is an object, and it converts like any other NSString.
			#expect(try rt.eval(#"(.init (.alloc (objc-class "NSMutableString")))"#) == "")
		}

		@Test func wrappersAreIdentity() throws {
			#expect(try rt.eval(#"(= (objc-class "NSString") (objc-class "NSString"))"#) == true)
			#expect(try rt.eval(#"(= (objc-class "NSString") (objc-class "NSNumber"))"#) == false)
		}

		// (.base target arg :label arg …): labels are syntax, arguments stay positional (design §5).
		@Test func methodForm() throws {
			#expect(try rt.eval(#"(.string-with-utf8-string (objc-class "NSString") "hi")"#) == "hi")
			#expect(try rt.eval(#"(.length (ns-string "abc"))"#) == 3)
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

		// A real Cocoa API reads the flag back: the enumeration ends where the block writes YES.
		@Test func aWriteThroughAPointerStopsAnEnumeration() throws {
			#expect(try rt.eval("""
			(let [seen (atom [])
			      blk (objc-block "v@?@Q^B" [x i stop]
			            (swap! seen conj x)
			            (objc-write! stop (= x "b")))]
			  (.enumerate-objects-using-block (ns-array ["a" "b" "c"]) blk)
			  @seen)
			""") == ["a", "b"])
			// Without the write the same enumeration runs to the end.
			#expect(try rt.eval("""
			(let [seen (atom [])
			      blk (objc-block "v@?@Q^B" [x i stop] (swap! seen conj x))]
			  (.enumerate-objects-using-block (ns-array ["a" "b" "c"]) blk)
			  @seen)
			""") == ["a", "b", "c"])
		}

		// The pointee's encoding travels with the handle, because it is what says how wide a write is.
		@Test func aPointeeTheBridgeCannotSizeIsRefused() throws {
			#expect(try rt.eval("""
			(let [err (atom nil)
			      blk (objc-block "v@?@Q^v" [x i stop]
			            (try (objc-write! stop true) (catch :default e (reset! err (ex-message e)))))]
			  (.enumerate-objects-using-block (ns-array ["a"]) blk)
			  @err)
			""") == "objc-write! writes a number or a boolean, and this pointer points at a 'v'")
			#expect(try rt.eval("""
			(let [err (atom nil)
			      blk (objc-block "v@?@Q^B" [x i stop]
			            (try (objc-write! stop "yes") (catch :default e (reset! err (ex-message e)))))]
			  (.enumerate-objects-using-block (ns-array ["a"]) blk)
			  @err)
			""") == "objc-write!: a string does not convert to 'B'")
			#expect(cljEvalError(#"(objc-write! (ns-string "x") 1)"#)?.contains("expects a pointer argument of a callback") == true)
			#expect(cljEvalError("(objc-write! 1 2)")?.contains("expects a pointer argument, got: long") == true)
		}

		// A wider shape writes past this frame, which is sized for the real struct: the sanitizer sees it.
		@Test func aStructReturnedThroughX8IsAsWideAsTheCallerThinks() throws {
			// -transformStruct is Foundation's own: the encoding, and the buffer, are its 48 bytes.
			let o = try cljEval(#"(objc-reify {:superclass "NSAffineTransform"} ("transform-struct" [self] [1.0 2.0 3.0 4.0 5.0 6.0]))"#)
			let t = unsafeBitCast(clj_objc_id(o.raw), to: NSAffineTransform.self)
			let s = t.transformStruct
			#expect([s.m11, s.m12, s.m21, s.m22, s.tX, s.tY] == [1, 2, 3, 4, 5, 6])
			// A size no shape in the table has is still refused, and the message names the shape.
			let refused = cljEvalError(#"(objc-reify {} (["odd" "{odd=sssssssss}@:"] [self] 1))"#)
			#expect(refused?.contains("returns a struct of 18 bytes through x8") == true)
			#expect(refused?.contains("{odd=sssssssss}@:") == true)
		}

		// A block Swift made: its invoke pointer and its descriptor's signature are all the call needs.
		@Test func callsABlockTheHostHandedUs() throws {
			let triple: @convention(block) (Int64) -> Int64 = { $0 * 3 }
			let greet: @convention(block) (NSString) -> NSString = { "hello \($0)" as NSString }
			let invoke = try rt.eval("objc-invoke")
			#expect(try invoke(Value(owning: clj_objc_wrap(unsafeBitCast(triple, to: UnsafeMutableRawPointer.self))), 14) == 42)
			#expect(try invoke(Value(owning: clj_objc_wrap(unsafeBitCast(greet, to: UnsafeMutableRawPointer.self))), Value("world")) == "hello world")
		}

		// Ours come back the same way, through Cocoa's storage and the descriptor, not through our cache.
		@Test func callsOurOwnBlocks() throws {
			#expect(try rt.eval("""
			(let [b (objc-block "q@?qq" [a c] (+ a c))
			      back (get (ns-dictionary->map (ns-dictionary {"b" b})) "b")]
			  [(objc-invoke b 20 22) (objc-invoke back 1 2)])
			""") == [42, 3])
			#expect(try rt.eval("""
			(let [p (objc-invoke (objc-block "{CGPoint=dd}@?d" [d] {:x d :y (* 2 d)}) 1.5)]
			  [(:x p) (:y p)])
			""") == [1.5, 3.0])
		}

		// No signature, so no prototype: the refusal is the answer. A global block is the shape of this one.
		@Test func aBlockWithoutASignatureIsRefused() throws {
			var descriptor: (UInt, UInt) = (0, 32)
			try withUnsafeMutablePointer(to: &descriptor) { desc in
				let global: AnyObject = try #require(NSClassFromString("__NSGlobalBlock__"))
				var layout = (isa: UnsafeRawPointer(Unmanaged.passUnretained(global).toOpaque()),
				              flags: Int32(1 << 28), reserved: Int32(0),
				              invoke: UnsafeRawPointer?.none, descriptor: UnsafeRawPointer(desc))
				try withUnsafeMutablePointer(to: &layout) { blk in
					let v = Value(owning: clj_objc_wrap(UnsafeMutableRawPointer(blk)))
					let invoke = try rt.eval("objc-invoke")
					#expect(throws: ClojureError.self) { _ = try invoke(v) }
					do { _ = try invoke(v) } catch let e as ClojureError {
						#expect(e.message.contains("carries no signature"))
					}
				}
			}
			#expect(cljEvalError(#"(objc-invoke (ns-string "x") 1)"#)?.contains("expects a block, got a") == true)
			#expect(cljEvalError(#"(objc-invoke (objc-block "v@?" []) 1)"#)?.contains("takes 0 argument(s), got 1") == true)
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
			  (go (let [s (ns-mutable-string "a")]
			        (<! (timeout 1))
			        (objc-send s "appendString:" "b")
			        (>! c (objc-send s "UTF8String"))))
			  (<!! c))
			""") == "ab")
		}
	}
}
