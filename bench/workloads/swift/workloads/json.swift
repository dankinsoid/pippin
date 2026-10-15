// @ai-generated(solo)
// workloads.json: the response decoded by JSONDecoder into Decodable structs, mapped to the app's model structs, and
// the screen's state encoded by JSONEncoder, as an iOS app does with Codable.
import Foundation

let firstNames = ["Anna", "Борис", "Chloé", "Dmitri", "Émile", "Фаина", "Grace", "Hiroshi", "Иван", "Jürgen", "Kateřina",
                  "Lucía"]
let lastNames = ["Smith", "Петрова", "Müller", "García", "Новиков", "Łukasiewicz", "O'Brien", "Søren", "Yamada", "Çelik"]
let words = ["the", "quick", "brown", "fox", "café", "naïve", "über", "привет", "мир", "東京", "data", "screen", "feed",
             "today", "день", "résumé", "jalapeño", "smörgåsbord", "news", "Ереван"]
let fragments = [#"\"quoted\""#, #"\n"#, #"\t"#, #"C:\\Users"#, "https://example.com/a/b?c=1",
                 #"caf\u00e9 \u043f\u0440\u0438\u0432\u0435\u0442"#]
let tagPool = ["news", "tech", "food", "travel", "music", "sport", "art", "science", "games", "books", "film", "photo"]
let kinds = ["post", "photo", "video", "repost", "ad"]
let currencies = ["USD", "EUR", "AMD", "RUB"]
let cities = ["Yerevan", "Lisbon", "Tbilisi", "Berlin", "Kyoto", "São Paulo", "Montréal", "Москва"]

// ---- the response's text, as the source writes it

func pick(_ v: [String], _ x: Int) -> String { v[x % v.count] }

func q(_ s: String) -> String { "\"" + s + "\"" }

func pad(_ n: Int, _ width: Int) -> String {
	let s = String(n)
	return String(repeating: "0", count: width - s.count) + s
}

func title(_ r: Int) -> String {
	(0..<(3 + r % 4)).map { pick(words, r / 7 + $0 * 5) }.joined(separator: " ")
}

func body(_ r: Int, _ i: Int) -> String {
	let nw = words.count
	return (0..<(12 + r % 20)).map { k in
		let t = (r / 3 + k * 11 + i) % (nw + fragments.count)
		return t < nw ? words[t] : fragments[t - nw]
	}.joined(separator: " ")
}

func rating(_ r: Int) -> String { "\(1 + r % 4).\(r / 4 % 10)\(1 + r / 40 % 9)" }

func coordinate(_ r: Int, _ sign: Bool, _ whole: Int) -> String {
	"\(sign ? "-" : "")\(1 + r / 3 % whole).\(pad(r / 240 % 1000, 3))\(1 + r / 240000 % 9)"
}

func authorJSON(_ r4: Int, _ r5: Int) -> String {
	let u = r4 % 400
	return "{\"id\":" + q("u-\(u)") + ",\"name\":" + q(pick(firstNames, r4) + " " + pick(lastNames, r4 / 12))
		+ ",\"handle\":" + q("@user\(u)") + ",\"avatarUrl\":" + q("https://cdn.example.com/avatars/\(u).jpg")
		+ ",\"verified\":\(r5 % 5 == 0),\"followers\":\(r5 % 100000)"
		+ ",\"bio\":" + (r5 % 3 == 0 ? "null" : q(pick(fragments, r5)))
		+ ",\"_links\":{\"self\":" + q("/v2/users/\(u)") + ",\"posts\":" + q("/v2/users/\(u)/posts") + "}}"
}

func mediaJSON(_ r: Int, _ i: Int, _ k: Int) -> String {
	"{\"type\":" + q(k % 2 == 0 ? "image" : "video") + ",\"url\":" + q("https://cdn.example.com/m/\(i)/\(k).jpg")
		+ ",\"width\":\(640 + 320 * ((r / 3 + k) % 4)),\"height\":\(480 + 240 * ((r / 12 + k) % 3))"
		+ ",\"alt\":" + ((r + k) % 2 == 0 ? "null" : q(pick(words, r + k)))
		+ ",\"blurhash\":\"LEHV6nWB2yk8pyo0adR*.7kCMdnj\"}"
}

func commentJSON(_ r: Int, _ i: Int, _ k: Int) -> String {
	"{\"id\":" + q("c-\(i)-\(k)") + ",\"author\":" + q(pick(firstNames, r + k)) + ",\"text\":" + q(title(r + k))
		+ ",\"likes\":\((r + k) % 50)}"
}

func postJSON(_ i: Int, _ r: ArraySlice<Int>) -> String {
	let r = Array(r)
	let kind = pick(kinds, r[0])
	var s = "{\"id\":" + q("p-\(100000 + i * 7)") + ",\"kind\":" + q(kind)
	s += ",\"createdAt\":" + q("2026-" + pad(1 + r[1] % 12, 2) + "-" + pad(1 + r[1] / 12 % 28, 2) + "T"
		+ pad(r[1] % 24, 2) + ":" + pad(r[1] / 24 % 60, 2) + ":00Z")
	s += ",\"title\":" + q(title(r[2])) + ",\"body\":" + q(body(r[3], i)) + ",\"author\":" + authorJSON(r[4], r[5])
	s += ",\"stats\":{\"likes\":\(r[6] % 5000),\"comments\":\(r[6] / 5000 % 300),\"shares\":\(r[7] % 100)"
		+ ",\"views\":\(100 + r[7] % 100000),\"rating\":" + rating(r[8]) + "}"
	s += ",\"tags\":[" + (0..<(r[9] % 4)).map { q(pick(tagPool, r[9] / 4 + $0 * 3)) }.joined(separator: ",") + "]"
	s += ",\"media\":[" + (0..<(r[10] % 3)).map { mediaJSON(r[10], i, $0) }.joined(separator: ",") + "]"
	s += ",\"price\":" + (r[11] % 4 == 0
		? "{\"amount\":" + q("\(r[11] / 8 % 200)." + pad(r[11] / 1600 % 100, 2)) + ",\"currency\":"
			+ q(pick(currencies, r[11] / 160000)) + "}"
		: "null")
	s += ",\"location\":" + (r[12] % 3 == 0
		? "{\"lat\":" + coordinate(r[12], false, 80) + ",\"lon\":" + coordinate(r[12] / 7, (r[12] / 5) % 2 == 0, 170)
			+ ",\"name\":" + q(pick(cities, r[12] / 11)) + "}"
		: "null")
	s += ",\"flags\":{\"pinned\":\(r[13] % 50 == 0),\"hidden\":\(r[13] % 17 == 0),\"sponsored\":\(kind == "ad")}"
	s += ",\"tracking\":{\"impressionId\":" + q("imp-\(r[14])") + ",\"position\":\(i)"
		+ ",\"experiment\":" + q("exp-\(r[14] % 7)") + ",\"variant\":\"b\"}"
	s += ",\"commentsPreview\":[" + (0..<(r[15] % 3)).map { commentJSON(r[15], i, $0) }.joined(separator: ",") + "]}"
	return s
}

func feedText(_ n: Int) -> String {
	// The generator's low bits cycle with a short period: a post's draws are its states' top 24 bits.
	let rs = lcgVec(seed: 42, count: 16 * n).map { $0 / 128 }
	return "{\"status\":\"ok\",\"generatedAt\":\"2026-10-15T12:00:00Z\",\"items\":["
		+ (0..<n).map { postJSON($0, rs[(16 * $0)..<(16 * ($0 + 1))]) }.joined(separator: ",")
		+ "],\"paging\":{\"cursor\":" + q("c-\(n)") + ",\"hasMore\":true,\"limit\":\(n)}"
		+ ",\"experiments\":{\"feedRanking\":\"v3\",\"inlineVideo\":false}}"
}

// What arrives from URLSession: the response's bytes, made once per process as the source's payload is.
let payload = Data(feedText(800).utf8)

// ---- the wire types: Decodable leaves out every key it does not declare

struct FeedDTO: Decodable {
	let items: [PostDTO]
	let paging: Paging
}

struct Paging: Codable {
	let cursor: String
	let hasMore: Bool
}

struct PostDTO: Decodable {
	let id, kind, createdAt, title, body: String
	let author: AuthorDTO
	let stats: Stats
	let tags: [String]
	let media: [MediaDTO]
	let price: PriceDTO?
	let location: Place?
	let flags: Flags
}

struct AuthorDTO: Decodable {
	let id, name, handle, avatarUrl: String
	let verified: Bool
	let followers: Int
}

struct Stats: Codable {
	let likes, comments, shares, views: Int
	let rating: Double
}

struct MediaDTO: Decodable {
	let type, url: String
	let width, height: Int
}

struct PriceDTO: Decodable {
	let amount, currency: String
}

struct Place: Codable {
	let lat, lon: Double
	let name: String
}

struct Flags: Decodable {
	let pinned, hidden: Bool
}

// ---- the app's model

enum Kind: String, Encodable { case post, photo, video, repost, ad }
enum MediaType: String, Encodable { case image, video }
enum Currency: String, Encodable { case USD, EUR, AMD, RUB }

func parseId(_ s: String) -> Int { Int(s[s.index(after: s.firstIndex(of: "-")!)...])! }

func cents(_ amount: String) -> Int {
	let parts = amount.split(separator: ".")
	return Int(parts[0])! * 100 + Int(parts[1])!
}

struct Author: Encodable {
	let id: Int
	let name, handle, avatarUrl: String
	let verified: Bool
	let followers: Int

	init(_ a: AuthorDTO) {
		id = parseId(a.id)
		name = a.name
		handle = a.handle
		avatarUrl = a.avatarUrl
		verified = a.verified
		followers = a.followers
	}
}

struct Media: Encodable {
	let type: MediaType
	let url: String
	let width, height: Int

	init(_ m: MediaDTO) {
		type = MediaType(rawValue: m.type)!
		url = m.url
		width = m.width
		height = m.height
	}
}

// Optionals encode only when present, as the source assocs them only when the response has them.
struct Post: Encodable {
	let id: Int
	let kind: Kind
	let createdAt, title, body: String
	let author: Author
	let stats: Stats
	let tags: [String]
	let media: [Media]
	let day: String
	let pinned, hidden: Bool
	var score = 0
	let priceCents: Int?
	let currency: Currency?
	let place: Place?

	init(_ p: PostDTO) {
		id = parseId(p.id)
		kind = Kind(rawValue: p.kind)!
		createdAt = p.createdAt
		title = p.title
		body = p.body
		author = Author(p.author)
		stats = p.stats
		tags = p.tags
		media = p.media.map(Media.init)
		day = String(p.createdAt.prefix(10))
		pinned = p.flags.pinned
		hidden = p.flags.hidden
		priceCents = p.price.map { cents($0.amount) }
		currency = p.price.map { Currency(rawValue: $0.currency)! }
		place = p.location
	}
}

struct Screen: Encodable {
	let posts: [Post]
	let paging: Paging
	let tagCounts: [String: Int]
	let authorCount: Int
}

func scored(_ p: Post) -> Post {
	var p = p
	let s: Stats = p.stats
	let bonus = p.pinned ? 1000 : 0
	p.score = s.likes + 2 * s.comments + 3 * s.shares + bonus
	return p
}

func screen(_ feed: FeedDTO) -> Screen {
	let posts: [Post] = feed.items.map(Post.init).filter { !$0.hidden }.map(scored).sorted { (a: Post, b: Post) in
		a.score != b.score ? a.score > b.score : a.id < b.id
	}
	var tagCounts: [String: Int] = [:]
	for p in posts {
		for t in p.tags { tagCounts[t, default: 0] += 1 }
	}
	let authors = Set(posts.map { (p: Post) in p.author.id })
	return Screen(posts: posts, paging: feed.paging, tagCounts: tagCounts, authorCount: authors.count)
}

func run() -> EDN {
	let feed = try! JSONDecoder().decode(FeedDTO.self, from: payload)
	let state = screen(feed)
	let encoder = JSONEncoder()
	encoder.outputFormatting = .withoutEscapingSlashes
	let out = try! encoder.encode(state)
	let posts = state.posts
	let top = posts[0]
	// Clojure's count of the written string: its UTF-8 bytes that start a character (BMP only: one UTF-16 unit each).
	let chars = out.reduce(0) { $0 + ($1 & 0xC0 == 0x80 ? 0 : 1) }
	return .vector([
		.int(feed.items.count), .int(posts.count), .int(state.authorCount), .int(state.tagCounts.count),
		.int(posts.filter(\.author.verified).count), .int(posts.reduce(0) { $0 + $1.score }),
		.int(posts.reduce(0) { $0 + ($1.priceCents ?? 0) }), .int(posts.filter { $0.place != nil }.count),
		.int(top.id), .string(top.title), .int(chars),
	])
}
