import SwiftUI
// SE-0309: an existential of a protocol with Self requirements is a legal type.
func takesAnyView(_ v: any View) -> String { String(describing: type(of: v)) }
func takesAnyEquatable(_ e: any Equatable) -> Bool { e is Int }
let r = takesAnyView(Text("x")) + String(takesAnyEquatable(1))
