// @ai-generated(solo)
#if canImport(Darwin)
import Darwin
#else
import Glibc
#endif
import Foundation

/// Plain POSIX sockets (docs/portability.md): the accept/read/write calls below are the same on Linux, the
/// only branch is `sockaddr_in.sin_len`, which only Darwin's struct has.
public struct SocketError: Error, CustomStringConvertible {
	public let description: String
	init(_ call: String) { description = "\(call): \(String(cString: strerror(errno)))" }
}

public final class TCPListener: @unchecked Sendable {
	private let fd: Int32
	private let stopLock = NSLock()
	private var stopped = false

	public init(host: String, port: UInt16) throws {
		#if canImport(Darwin)
		fd = socket(AF_INET, SOCK_STREAM, 0)
		#else
		fd = socket(AF_INET, Int32(SOCK_STREAM.rawValue), 0)
		#endif
		guard fd >= 0 else { throw SocketError("socket") }
		var yes: Int32 = 1
		setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, socklen_t(MemoryLayout<Int32>.size))
		var addr = sockaddr_in()
		addr.sin_family = sa_family_t(AF_INET)
		addr.sin_port = port.bigEndian
		addr.sin_addr.s_addr = host.withCString { inet_addr($0) }
		#if canImport(Darwin)
		addr.sin_len = UInt8(MemoryLayout<sockaddr_in>.size)
		#endif
		let bound = withUnsafePointer(to: &addr) { p in
			p.withMemoryRebound(to: sockaddr.self, capacity: 1) { bind(fd, $0, socklen_t(MemoryLayout<sockaddr_in>.size)) }
		}
		guard bound == 0 else { throw SocketError("bind") }
		guard listen(fd, 64) == 0 else { throw SocketError("listen") }
	}

	/// The bound port; resolves an ephemeral one after binding to port 0.
	public var port: UInt16 {
		var addr = sockaddr_in()
		var len = socklen_t(MemoryLayout<sockaddr_in>.size)
		withUnsafeMutablePointer(to: &addr) { p in
			p.withMemoryRebound(to: sockaddr.self, capacity: 1) { _ = getsockname(fd, $0, &len) }
		}
		return UInt16(bigEndian: addr.sin_port)
	}

	public func accept() throws -> TCPConnection {
		#if canImport(Darwin)
		let client = Darwin.accept(fd, nil, nil)
		#else
		let client = Glibc.accept(fd, nil, nil)
		#endif
		guard client >= 0 else { throw SocketError("accept") }
		return TCPConnection(fd: client)
	}

	/// Closes the listening socket; a subsequent `accept()` fails and `isStopped` tells the caller to give up.
	public func stop() {
		stopLock.withLock { stopped = true }
		#if canImport(Darwin)
		_ = Darwin.close(fd)
		#else
		_ = Glibc.close(fd)
		#endif
	}

	public var isStopped: Bool { stopLock.withLock { stopped } }
}

public final class TCPConnection: @unchecked Sendable {
	private let fd: Int32

	init(fd: Int32) {
		self.fd = fd
		var one: Int32 = 1
		setsockopt(fd, Int32(IPPROTO_TCP), TCP_NODELAY, &one, socklen_t(MemoryLayout<Int32>.size))
	}

	/// A client connection, for a test driving the server as a real nREPL client would.
	public convenience init(connectingTo host: String, port: UInt16) throws {
		#if canImport(Darwin)
		let fd = socket(AF_INET, SOCK_STREAM, 0)
		#else
		let fd = socket(AF_INET, Int32(SOCK_STREAM.rawValue), 0)
		#endif
		guard fd >= 0 else { throw SocketError("socket") }
		var addr = sockaddr_in()
		addr.sin_family = sa_family_t(AF_INET)
		addr.sin_port = port.bigEndian
		addr.sin_addr.s_addr = host.withCString { inet_addr($0) }
		#if canImport(Darwin)
		addr.sin_len = UInt8(MemoryLayout<sockaddr_in>.size)
		#endif
		let connected = withUnsafePointer(to: &addr) { p in
			p.withMemoryRebound(to: sockaddr.self, capacity: 1) { connect(fd, $0, socklen_t(MemoryLayout<sockaddr_in>.size)) }
		}
		guard connected == 0 else { throw SocketError("connect") }
		self.init(fd: fd)
	}

	/// Blocking read; an empty result means the peer closed (or the socket errored).
	public func read() -> [UInt8] {
		var buf = [UInt8](repeating: 0, count: 65536)
		let n = buf.withUnsafeMutableBytes { p in recv(fd, p.baseAddress, p.count, 0) }
		guard n > 0 else { return [] }
		return Array(buf[0..<n])
	}

	public func writeAll(_ bytes: [UInt8]) throws {
		var offset = 0
		try bytes.withUnsafeBytes { p in
			while offset < bytes.count {
				let n = send(fd, p.baseAddress!.advanced(by: offset), bytes.count - offset, 0)
				guard n > 0 else { throw SocketError("send") }
				offset += n
			}
		}
	}

	public func close() {
		#if canImport(Darwin)
		_ = Darwin.close(fd)
		#else
		_ = Glibc.close(fd)
		#endif
	}
}
