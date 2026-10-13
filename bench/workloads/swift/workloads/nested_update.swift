// @ai-generated(solo)
// workloads.nested-update: the application state as structs and dictionaries, updated in place.

// A key the source reads through fnil, a get-in default of 0 or keep-then-sum is a field defaulting to 0 or [].
struct User {
	var name: String?
	var visits = 0
	var tags: [Int] = []
	var seen: Int?
	var score = 0
}

struct AppState {
	var users: [Int: User] = [:]
	var log: [Int] = []
	var ok = 0
	var err = 0
	var index: [Int: [Int: Int]] = [:]

	mutating func step(_ x: Int) {
		let uid = x % 500
		switch (x / 7) % 6 {
		case 0:
			users[uid, default: User()].name = "user-\(uid)"
		case 1:
			users[uid, default: User()].visits += 1
		case 2:
			users[uid, default: User()].tags.append(x % 10)
		case 3:
			if x.isMultiple(of: 2) { ok += 1 } else { err += 1 }
			if log.count >= 100 {
				log.removeFirst()
			}
			log.append(uid)
		case 4:
			users[uid, default: User()].seen = x
			users[uid, default: User()].score += x % 13
		default:
			index[x % 50, default: [:]][x % 7] = uid
		}
	}
}

func run() -> EDN {
	var st = AppState()
	var x = 1
	var reads = 0
	for _ in 0..<200000 {
		x = lcg(x)
		reads += (st.users[x % 500]?.visits ?? 0) + (st.index[x % 50]?.count ?? 0)
		st.step(x)
	}
	let users = st.users.values
	return .ints([
		users.count,
		users.reduce(0) { $0 + $1.visits },
		users.reduce(0) { $0 + $1.tags.count },
		users.reduce(0) { $0 + $1.score },
		st.ok,
		st.err,
		st.log.reduce(0, +),
		st.index.values.reduce(0) { $0 + $1.count },
		reads,
	])
}
