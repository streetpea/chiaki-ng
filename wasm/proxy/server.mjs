#!/usr/bin/env node
import http from "node:http";
import https from "node:https";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import crypto from "node:crypto";
import dgram from "node:dgram";
import net from "node:net";
import dns from "node:dns/promises";
import { execFileSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import zlib from "node:zlib";
import { loadEnv } from "./env.mjs";
import { openStore } from "./store.mjs";
import { createShareHub } from "./share.mjs";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const cfg = loadEnv();
const store = openStore(cfg);
const PORT = cfg.port;
const ROOT = process.env.CHIAKI_WASM_ROOT || (
	fs.existsSync(path.join(__dirname, "chiaki.wasm"))
		? path.resolve(__dirname)
		: path.resolve(__dirname, "..", "..", "build-wasm", "wasm")
);
const WWW = process.env.CHIAKI_WASM_WWW || path.resolve(__dirname, "..", "..", "wasm", "www");
const CERT_DIR = path.join(__dirname, "certs");
const CERT_FILE = path.join(CERT_DIR, "cert.pem");
const KEY_FILE = path.join(CERT_DIR, "key.pem");

function lanIPv4() {
	const out = [];
	for (const list of Object.values(os.networkInterfaces())) {
		for (const n of list || []) {
			const family = n.family === "IPv4" || n.family === 4;
			if (family && !n.internal) out.push(n.address);
		}
	}
	return [...new Set(out)];
}

function opensslBin() {
	const candidates = [
		process.env.OPENSSL_PATH,
		"openssl",
		"C:\\Program Files\\Git\\usr\\bin\\openssl.exe",
		"C:\\Program Files\\Git\\mingw64\\bin\\openssl.exe"
	].filter(Boolean);
	for (const bin of candidates) {
		try {
			execFileSync(bin, ["version"], { stdio: "ignore" });
			return bin;
		} catch {}
	}
	return null;
}

function certCoversIps(certPem, ips) {
	try {
		const x509 = new crypto.X509Certificate(certPem);
		return ips.every((ip) => {
			try { return x509.checkIP(ip); } catch { return false; }
		});
	} catch {
		return false;
	}
}

function ensureTlsOptions() {
	const ips = lanIPv4();
	fs.mkdirSync(CERT_DIR, { recursive: true });
	if (fs.existsSync(CERT_FILE) && fs.existsSync(KEY_FILE)) {
		const cert = fs.readFileSync(CERT_FILE);
		const key = fs.readFileSync(KEY_FILE);
		if (certCoversIps(cert, ["127.0.0.1", ...ips]))
			return { key, cert, ips };
	}
	const openssl = opensslBin();
	if (!openssl) {
		console.warn("openssl introuvable : HTTPS LAN désactivé. Installez Git for Windows ou OpenSSL.");
		return null;
	}
	const cnf = path.join(CERT_DIR, "openssl.cnf");
	const alt = ["DNS.1 = localhost", "IP.1 = 127.0.0.1"]
		.concat(ips.map((ip, i) => `IP.${i + 2} = ${ip}`))
		.join("\n");
	fs.writeFileSync(cnf, `[req]
distinguished_name = dn
x509_extensions = v3
prompt = no
[dn]
CN = Chiaki WASM
[v3]
subjectAltName = @alt
basicConstraints = CA:FALSE
keyUsage = digitalSignature, keyEncipherment
extendedKeyUsage = serverAuth
[alt]
${alt}
`);
	try {
		execFileSync(openssl, [
			"req", "-x509", "-newkey", "rsa:2048", "-sha256", "-days", "825", "-nodes",
			"-keyout", KEY_FILE, "-out", CERT_FILE, "-config", cnf, "-extensions", "v3"
		], { stdio: ["ignore", "ignore", "pipe"] });
	} catch (err) {
		console.warn("Échec de la génération du certificat HTTPS:", err.stderr?.toString() || err.message);
		return null;
	}
	return {
		key: fs.readFileSync(KEY_FILE),
		cert: fs.readFileSync(CERT_FILE),
		ips
	};
}

const HDR = 28;
const T = {
	SOCKET: 1, CLOSE: 2, BIND: 3, CONNECT: 4, SEND: 5, SENDTO: 6,
	SETSOCKOPT: 7, GETSOCKNAME: 8, GETADDRINFO: 9, SHUTDOWN: 10,
	REPLY: 128, PUSH_DATA: 129, PUSH_CONNECTED: 130, PUSH_CLOSED: 131, PUSH_ERROR: 132
};
const SOCK_STREAM = 1;
const SOCK_DGRAM = 2;

function readHdr(buf) {
	return {
		type: buf.readUInt8(0),
		id: buf.readUInt32LE(4),
		fd: buf.readInt32LE(8),
		a: buf.readInt32LE(12),
		b: buf.readInt32LE(16),
		c: buf.readInt32LE(20),
		len: buf.readUInt32LE(24)
	};
}

function encode(type, id, fd, a, b, c, payload) {
	const plen = payload ? payload.length : 0;
	const buf = Buffer.alloc(HDR + plen);
	buf.writeUInt8(type, 0);
	buf.writeUInt32LE(id >>> 0, 4);
	buf.writeInt32LE(fd, 8);
	buf.writeUInt32LE(a >>> 0, 12);
	buf.writeUInt32LE(b >>> 0, 16);
	buf.writeUInt32LE(c >>> 0, 20);
	buf.writeUInt32LE(plen, 24);
	if (plen) payload.copy(buf, HDR);
	return buf;
}

const AGENT_HDR = 16;
const A = {
	OPEN: 1, CLOSE: 2, DATA: 3, PING: 4, PONG: 5,
	PORTCHECK: 6, PORTCHECK_RES: 7, HELLO: 8, HELLO_OK: 9,
	REJECTED: 10, APPROVED: 11
};

function encodeAgent(type, sid, extra, payload) {
	const plen = payload ? payload.length : 0;
	const buf = Buffer.alloc(AGENT_HDR + plen);
	buf.writeUInt8(type, 0);
	buf.writeUInt32LE(sid >>> 0, 4);
	buf.writeUInt32LE(plen, 8);
	buf.writeUInt32LE(extra >>> 0, 12);
	if (plen) payload.copy(buf, AGENT_HDR);
	return buf;
}

function readAgent(buf) {
	if (buf.length < AGENT_HDR) return null;
	const len = buf.readUInt32LE(8);
	return {
		type: buf.readUInt8(0),
		sid: buf.readUInt32LE(4),
		len,
		extra: buf.readUInt32LE(12),
		payload: buf.subarray(AGENT_HDR, AGENT_HDR + len)
	};
}

function wsAccept(key) {
	return crypto.createHash("sha1")
		.update(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11")
		.digest("base64");
}

function decodeWsFrame(buffer) {
	if (buffer.length < 2) return null;
	const opcode = buffer[0] & 0x0f;
	const masked = (buffer[1] & 0x80) !== 0;
	let len = buffer[1] & 0x7f;
	let offset = 2;
	if (len === 126) {
		if (buffer.length < 4) return null;
		len = buffer.readUInt16BE(2);
		offset = 4;
	} else if (len === 127) {
		if (buffer.length < 10) return null;
		len = Number(buffer.readBigUInt64BE(2));
		offset = 10;
	}
	let mask;
	if (masked) {
		if (buffer.length < offset + 4) return null;
		mask = buffer.subarray(offset, offset + 4);
		offset += 4;
	}
	if (buffer.length < offset + len) return null;
	let payload = buffer.subarray(offset, offset + len);
	if (masked) {
		payload = Buffer.from(payload);
		for (let i = 0; i < payload.length; i++) payload[i] ^= mask[i % 4];
	}
	return { opcode, payload, rest: buffer.subarray(offset + len) };
}

function encodeWsFrame(payload) {
	const len = payload.length;
	let header;
	if (len < 126) {
		header = Buffer.alloc(2);
		header[0] = 0x82;
		header[1] = len;
	} else if (len < 65536) {
		header = Buffer.alloc(4);
		header[0] = 0x82;
		header[1] = 126;
		header.writeUInt16BE(len, 2);
	} else {
		header = Buffer.alloc(10);
		header[0] = 0x82;
		header[1] = 127;
		header.writeBigUInt64BE(BigInt(len), 2);
	}
	return Buffer.concat([header, payload]);
}

function ipToInt(ip) {
	const p = ip.split(".").map((x) => Number(x));
	if (p.length !== 4) return 0;
	return (p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24)) >>> 0;
}

function intToIp(n) {
	n >>>= 0;
	return `${n & 255}.${(n >>> 8) & 255}.${(n >>> 16) & 255}.${(n >>> 24) & 255}`;
}

class PosixBridge {
	constructor(send) {
		this.send = send;
		this.socks = new Map();
	}

	reply(h, result, err = 0, extra = 0, payload = null) {
		this.send(encode(T.REPLY, h.id, h.fd, result, err, extra, payload));
	}

	pushData(fd, port, addr, payload) {
		this.send(encode(T.PUSH_DATA, 0, fd, 0, port, addr, payload));
	}

	async handle(buf) {
		if (buf.length < HDR) return;
		const h = readHdr(buf);
		const payload = buf.subarray(HDR, HDR + h.len);
		try {
			await this.dispatch(h, payload);
		} catch (e) {
			this.reply(h, -1, 5);
			console.error("posix-net", e);
		}
	}

	async dispatch(h, payload) {
		switch (h.type) {
			case T.SOCKET: return this.opSocket(h);
			case T.CLOSE: return this.opClose(h);
			case T.BIND: return this.opBind(h);
			case T.CONNECT: return this.opConnect(h);
			case T.SEND:
			case T.SENDTO: return this.opSend(h, payload);
			case T.SETSOCKOPT: return this.opSetsockopt(h);
			case T.GETSOCKNAME: return this.opGetsockname(h);
			case T.GETADDRINFO: return this.opGetaddrinfo(h, payload);
			case T.SHUTDOWN: return this.opShutdown(h);
			default: return this.reply(h, -1, 22);
		}
	}

	attachUdp(fd, rec) {
		rec.udp.on("message", (msg, rinfo) => {
			this.pushData(fd, rinfo.port, ipToInt(rinfo.address), msg);
		});
		rec.udp.on("error", (err) => {
			if (rec.binding) return;
			console.error("udp", fd, err.message);
		});
		try { rec.udp.setRecvBufferSize(4 * 1024 * 1024); } catch {}
		try { rec.udp.setSendBufferSize(4 * 1024 * 1024); } catch {}
		try { rec.udp.setBroadcast(true); } catch {}
	}

	replaceUdp(fd, rec) {
		if (rec.udp) {
			try { rec.udp.removeAllListeners(); } catch {}
			try { rec.udp.close(); } catch {}
		}
		rec.udp = dgram.createSocket({ type: "udp4", reuseAddr: true });
		rec.boundPort = 0;
		rec.binding = false;
		this.attachUdp(fd, rec);
	}

	ensureUdp(fd) {
		let rec = this.socks.get(fd);
		if (rec?.udp) return rec;
		rec = rec || { type: SOCK_DGRAM, remotePort: 0, remoteAddr: "0.0.0.0", boundPort: 0, binding: false };
		this.replaceUdp(fd, rec);
		this.socks.set(fd, rec);
		return rec;
	}

	opSocket(h) {
		const type = h.b;
		if (type === SOCK_DGRAM) {
			this.ensureUdp(h.fd);
			if (h.id) this.reply(h, 0);
			return;
		}
		if (type === SOCK_STREAM) {
			this.socks.set(h.fd, { type, remotePort: 0, remoteAddr: "0.0.0.0", boundPort: 0, tcp: null });
			if (h.id) this.reply(h, 0);
			return;
		}
		return this.reply(h, -1, 93);
	}

	opClose(h) {
		const rec = this.socks.get(h.fd);
		if (rec?.udp) rec.udp.close();
		if (rec?.tcp) rec.tcp.destroy();
		this.socks.delete(h.fd);
		if (h.id) this.reply(h, 0);
	}

	opBind(h) {
		const rec = this.ensureUdp(h.fd);
		if (rec.boundPort) {
			if (h.id) this.reply(h, 0);
			return;
		}
		let settled = false;
		const finish = (ok) => {
			if (settled) return;
			settled = true;
			rec.binding = false;
			if (h.id) this.reply(h, ok ? 0 : -1, ok ? 0 : 98);
		};
		rec.binding = true;
		const sock = rec.udp;
		const timer = setTimeout(() => {
			try {
				rec.boundPort = sock.address().port;
				finish(true);
			} catch { finish(false); }
		}, 600);
		const onErr = (err) => {
			clearTimeout(timer);
			const msg = String(err && err.message || "");
			if (/already/i.test(msg)) {
				try { rec.boundPort = sock.address().port; finish(true); return; } catch {}
			}
			finish(false);
		};
		sock.once("error", onErr);
		try {
			sock.bind({ port: 0, address: "0.0.0.0", exclusive: false }, () => {
				clearTimeout(timer);
				try { sock.removeListener("error", onErr); } catch {}
				try { rec.boundPort = sock.address().port; } catch { rec.boundPort = 1; }
				finish(true);
			});
		} catch (e) {
			onErr(e);
		}
	}

	opConnect(h) {
		let rec = this.socks.get(h.fd);
		if (!rec) rec = this.ensureUdp(h.fd);
		const port = h.b;
		const ip = intToIp(h.c >>> 0);
		rec.remotePort = port;
		rec.remoteAddr = ip;
		if (rec.udp) {
			if (h.id) this.reply(h, 0);
			return;
		}
		const sock = net.connect({ host: ip, port }, () => {
			rec.connected = true;
			this.send(encode(T.PUSH_CONNECTED, 0, h.fd, 0, 0, 0, null));
			if (h.id) this.reply(h, 0);
		});
		sock.on("data", (chunk) => this.pushData(h.fd, port, ipToInt(ip), chunk));
		sock.on("close", () => this.send(encode(T.PUSH_CLOSED, 0, h.fd, 0, 0, 0, null)));
		sock.on("error", (err) => {
			console.error("tcp", h.fd, err.message);
			if (h.id && !rec.connected) this.reply(h, -1, 111);
			this.send(encode(T.PUSH_ERROR, 0, h.fd, 0, 0, 0, null));
		});
		rec.tcp = sock;
	}

	opSend(h, payload) {
		const rec = this.socks.get(h.fd) || (h.type === T.SENDTO ? this.ensureUdp(h.fd) : null);
		if (!rec) return;
		if (rec.udp) {
			const port = h.b || rec.remotePort;
			const ip = h.c ? intToIp(h.c >>> 0) : rec.remoteAddr;
			rec.udp.send(payload, port, ip);
		} else if (rec.tcp) {
			rec.tcp.write(payload);
		}
		if (h.id) this.reply(h, payload.length);
	}

	opSetsockopt(h) {
		const rec = this.socks.get(h.fd);
		if (rec?.udp && h.b === 6) {
			try { rec.udp.setBroadcast(!!h.c); } catch {}
		}
		if (h.id) this.reply(h, 0);
	}

	opGetsockname(h) {
		const rec = this.socks.get(h.fd);
		if (!rec) return this.reply(h, -1, 9);
		let port = rec.boundPort || 0;
		let addr = 0;
		try {
			if (rec.udp) {
				const a = rec.udp.address();
				port = a.port;
				addr = ipToInt(a.address === "0.0.0.0" ? "0.0.0.0" : a.address);
			} else if (rec.tcp) {
				const a = rec.tcp.localAddress ? { port: rec.tcp.localPort, address: rec.tcp.localAddress } : { port: 0, address: "0.0.0.0" };
				port = a.port;
				addr = ipToInt(a.address);
			}
		} catch {}
		this.reply(h, port, 0, addr);
	}

	async opGetaddrinfo(h, payload) {
		const host = payload.toString("utf8").replace(/\0+$/, "");
		try {
			const r = await dns.lookup(host, { family: 4 });
			const ip = ipToInt(r.address);
			const buf = Buffer.alloc(4);
			buf.writeUInt32LE(ip, 0);
			this.reply(h, ip, 0, 0, buf);
		} catch {
			this.reply(h, -1, 0);
		}
	}

	opShutdown(h) {
		const rec = this.socks.get(h.fd);
		try { rec?.tcp?.end(); } catch {}
		if (h.id) this.reply(h, 0);
	}

	closeAll() {
		for (const rec of this.socks.values()) {
			try { rec.udp?.close(); } catch {}
			try { rec.tcp?.destroy(); } catch {}
		}
		this.socks.clear();
	}
}

const MIME = {
	".html": "text/html; charset=utf-8",
	".js": "text/javascript; charset=utf-8",
	".mjs": "text/javascript; charset=utf-8",
	".css": "text/css; charset=utf-8",
	".wasm": "application/wasm",
	".json": "application/json",
	".svg": "image/svg+xml",
	".png": "image/png",
	".ico": "image/x-icon"
};

function isInside(root, file) {
	const base = path.resolve(root);
	const resolved = path.resolve(file);
	return resolved === base || resolved.startsWith(base + path.sep);
}

function resolvePublicFile(urlPath) {
	const name = path.basename(urlPath);
	const wasmBin = name.startsWith("chiaki") && (name.endsWith(".wasm") || name.endsWith(".js"));
	const wwwFile = path.normalize(path.join(WWW, urlPath));
	const rootFile = path.normalize(path.join(ROOT, urlPath));
	if (!wasmBin && isInside(WWW, wwwFile) && fs.existsSync(wwwFile) && fs.statSync(wwwFile).isFile())
		return wwwFile;
	if (isInside(ROOT, rootFile)) return rootFile;
	return null;
}

function sendFile(req, res, file) {
	const corp = {
		"Cross-Origin-Opener-Policy": "same-origin",
		"Cross-Origin-Embedder-Policy": "require-corp",
		"Cross-Origin-Resource-Policy": "same-origin"
	};
	fs.stat(file, (err, st) => {
		if (err || !st.isFile()) {
			res.writeHead(404, corp);
			res.end("not found");
			return;
		}
		const ext = path.extname(file);
		const etag = `"${st.size.toString(16)}-${Math.trunc(st.mtimeMs).toString(16)}"`;
		const headers = {
			...corp,
			ETag: etag,
			"Content-Type": MIME[ext] || "application/octet-stream"
		};
		headers["Cache-Control"] = [".html", ".js", ".mjs", ".css", ".json"].includes(ext)
			? "no-store"
			: "public, max-age=60, must-revalidate";
		if (req.headers["if-none-match"] === etag) {
			res.writeHead(304, headers);
			res.end();
			return;
		}
		const src = fs.createReadStream(file);
		src.on("error", () => {
			if (!res.headersSent) res.writeHead(500, corp);
			res.end();
		});
		const wantsGzip = /\bgzip\b/.test(String(req.headers["accept-encoding"] || ""));
		const base = path.basename(file);
		const skipGzip = [".png", ".wasm", ".woff", ".woff2", ".exe"].includes(ext)
			|| base.startsWith("chiaki-proxy");
		if (base.startsWith("chiaki-proxy")) {
			headers["Content-Disposition"] = `attachment; filename="${base}"`;
			headers["Cache-Control"] = "no-store";
		}
		if (wantsGzip && !skipGzip) {
			headers["Content-Encoding"] = "gzip";
			headers.Vary = "Accept-Encoding";
			res.writeHead(200, headers);
			src.pipe(zlib.createGzip({ level: 5 })).pipe(res);
			return;
		}
		headers["Content-Length"] = st.size;
		res.writeHead(200, headers);
		src.pipe(res);
	});
}

function lookupPsnAccountId(username) {
	const headers = {
		Accept: "application/json",
		"User-Agent": "Mozilla/5.0 Chiaki-NG-WASM"
	};
	const get = (url, redirects) => new Promise((resolve, reject) => {
		https.get(url, { headers }, (r) => {
			if (r.statusCode >= 300 && r.statusCode < 400 && r.headers.location && redirects > 0) {
				r.resume();
				resolve(get(new URL(r.headers.location, url).href, redirects - 1));
				return;
			}
			let data = "";
			r.on("data", (c) => { data += c; });
			r.on("end", () => resolve({ status: r.statusCode || 500, data, type: r.headers["content-type"] || "" }));
		}).on("error", reject);
	});
	return get(`https://psn.flipscreen.games/search.php?username=${encodeURIComponent(username)}`, 5).then(({ status, data, type }) => {
		const trimmed = String(data || "").trim();
		if (trimmed.startsWith("{") || /json/i.test(type)) {
			try {
				return { status, body: JSON.parse(trimmed) };
			} catch {
				return { status: 502, body: { error: "invalid JSON from lookup service" } };
			}
		}
		if (status >= 500)
			return { status, body: { error: "lookup service unavailable" } };
		return { status: status >= 400 ? status : 502, body: { error: "lookup service returned HTML instead of JSON" } };
	});
}

function readCookie(req, name) {
	const raw = String(req.headers.cookie || "");
	for (const part of raw.split(";")) {
		const trimmed = part.trim();
		if (!trimmed) continue;
		const eq = trimmed.indexOf("=");
		if (eq < 1) continue;
		if (trimmed.slice(0, eq) === name)
			return decodeURIComponent(trimmed.slice(eq + 1));
	}
	return "";
}

function sessionCookie(token, maxAgeSec, secure) {
	const parts = [
		`chiaki_sid=${encodeURIComponent(token)}`,
		"Path=/",
		"HttpOnly",
		"SameSite=Lax",
		`Max-Age=${Math.max(0, maxAgeSec | 0)}`
	];
	if (secure) parts.push("Secure");
	return parts.join("; ");
}

const shareHub = createShareHub({ store, decodeWsFrame, wsAccept, readCookie });

class HomeAgents {
	constructor() {
		this.byUser = new Map();
		this.nextSid = 1;
	}

	online(userId) {
		const rec = this.byUser.get(userId);
		return !!(rec && rec.approved);
	}

	info(userId) {
		const rec = this.byUser.get(userId);
		if (!rec) return { homeProxy: false, homeProxyPending: false, homeProxyName: "" };
		return {
			homeProxy: !!rec.approved,
			homeProxyPending: !rec.approved,
			homeProxyName: rec.pcName || ""
		};
	}

	dropUser(userId, dyingSocket) {
		const rec = this.byUser.get(userId);
		if (!rec) return;
		if (dyingSocket && rec.socket !== dyingSocket) return;
		try { clearInterval(rec.pingTimer); } catch {}
		for (const session of rec.sessions.values()) {
			try { session.socket.end(); } catch {}
			try { session.socket.destroy(); } catch {}
		}
		rec.sessions.clear();
		for (const fn of rec.pending.values()) {
			try { fn(0); } catch {}
		}
		rec.pending.clear();
		if (this.byUser.get(userId) === rec)
			this.byUser.delete(userId);
		if (rec.socket && rec.socket !== dyingSocket) {
			try { rec.socket.end(); } catch {}
			try { rec.socket.destroy(); } catch {}
		}
	}

	send(rec, buf) {
		try { rec.socket.write(encodeWsFrame(buf)); } catch {}
	}

	closeSession(rec, sid) {
		const session = rec.sessions.get(sid);
		if (!session) return;
		rec.sessions.delete(sid);
		this.send(rec, encodeAgent(A.CLOSE, sid, 0, null));
		try { session.socket.end(); } catch {}
		try { session.socket.destroy(); } catch {}
	}

	pipePosix(user, socket) {
		const rec = this.byUser.get(user.id);
		if (!rec || !rec.approved) return false;
		const sid = this.nextSid++;
		const session = { socket, acc: Buffer.alloc(0) };
		rec.sessions.set(sid, session);
		this.send(rec, encodeAgent(A.OPEN, sid, 0, null));
		console.log(`Home proxy: session ${sid} → agent user ${user.id}`);
		const onClose = () => {
			if (!rec.sessions.has(sid)) return;
			rec.sessions.delete(sid);
			this.send(rec, encodeAgent(A.CLOSE, sid, 0, null));
		};
		socket.on("data", (chunk) => {
			session.acc = Buffer.concat([session.acc, chunk]);
			for (;;) {
				const frame = decodeWsFrame(session.acc);
				if (!frame) break;
				session.acc = frame.rest;
				if (frame.opcode === 0x8) {
					onClose();
					try { socket.end(); } catch {}
					return;
				}
				if (frame.opcode === 0x9) {
					const pong = Buffer.from(frame.payload);
					socket.write(Buffer.concat([Buffer.from([0x8a, pong.length]), pong]));
					continue;
				}
				if (frame.opcode === 0x2 || frame.opcode === 0x1)
					this.send(rec, encodeAgent(A.DATA, sid, 0, frame.payload));
			}
		});
		socket.on("close", onClose);
		socket.on("error", onClose);
		return true;
	}

	portCheck(userId, host, port, ms) {
		return new Promise((resolve) => {
			const rec = this.byUser.get(userId);
			const fail = (status) => resolve({
				ports: [{ port, proto: "tcp", role: "session", status }],
				via: "home"
			});
			if (!rec) return fail("filtered");
			if (!rec.approved) return fail("filtered");
			const sid = this.nextSid++;
			const timer = setTimeout(() => {
				rec.pending.delete(sid);
				fail("filtered");
			}, ms);
			rec.pending.set(sid, (extra) => {
				clearTimeout(timer);
				const status = extra === 1 ? "open" : extra === 2 ? "closed" : "filtered";
				resolve({
					ports: [{ port, proto: "tcp", role: "session", status }],
					via: "home"
				});
			});
			this.send(rec, encodeAgent(A.PORTCHECK, sid, port, Buffer.from(String(host), "utf8")));
		});
	}

	handleAgentFrame(rec, user, payload) {
		const msg = readAgent(payload);
		if (!msg) return;
		if (msg.type === A.PONG) return;
		if (msg.type === A.PING) {
			this.send(rec, encodeAgent(A.PONG, msg.sid, 0, null));
			return;
		}
		if (msg.type === A.HELLO) {
			let pcName = "";
			try {
				const j = JSON.parse(msg.payload.toString("utf8"));
				pcName = String(j && j.name || "").trim().slice(0, 64);
			} catch {}
			rec.pcName = pcName;
			const name = String(user.username || user.email || user.id);
			const body = Buffer.from(JSON.stringify({
				ok: true,
				user: name,
				email: user.email || null,
				approved: !!rec.approved
			}), "utf8");
			this.send(rec, encodeAgent(A.HELLO_OK, 0, rec.approved ? 1 : 0, body));
			return;
		}
		if (msg.type === A.PORTCHECK_RES) {
			const fn = rec.pending.get(msg.sid);
			if (fn) {
				rec.pending.delete(msg.sid);
				fn(msg.extra);
			}
			return;
		}
		if (msg.type === A.CLOSE) {
			const session = rec.sessions.get(msg.sid);
			if (!session) return;
			rec.sessions.delete(msg.sid);
			try { session.socket.end(); } catch {}
			try { session.socket.destroy(); } catch {}
			return;
		}
		if (msg.type === A.DATA) {
			const session = rec.sessions.get(msg.sid);
			if (!session) return;
			try { session.socket.write(encodeWsFrame(msg.payload)); } catch {}
		}
	}

	attach(req, socket, user) {
		this.dropUser(user.id);
		const rec = {
			socket,
			userId: user.id,
			sessions: new Map(),
			pending: new Map(),
			acc: Buffer.alloc(0),
			helloAt: Date.now(),
			pingTimer: null
		};
		this.byUser.set(user.id, rec);
		rec.approved = false;
		rec.pcName = "";
		const who = user.email || user.username || user.id;
		const ip = clientIpv4(req) || req.socket?.remoteAddress || "";
		console.log(`Home proxy: agent connecté (${who}) depuis ${ip || "?"}`);
		rec.pingTimer = setInterval(() => {
			this.send(rec, encodeAgent(A.PING, 0, 0, null));
		}, 20000);
		socket.on("data", (chunk) => {
			rec.acc = Buffer.concat([rec.acc, chunk]);
			for (;;) {
				const frame = decodeWsFrame(rec.acc);
				if (!frame) break;
				rec.acc = frame.rest;
				if (frame.opcode === 0x8) {
					this.dropUser(user.id);
					try { socket.end(); } catch {}
					return;
				}
				if (frame.opcode === 0x9) {
					const pong = Buffer.from(frame.payload);
					socket.write(Buffer.concat([Buffer.from([0x8a, pong.length]), pong]));
					continue;
				}
				if (frame.opcode === 0xa) continue;
				if (frame.opcode === 0x2 || frame.opcode === 0x1)
					this.handleAgentFrame(rec, user, frame.payload);
			}
		});
		socket.on("close", () => {
			if (this.byUser.get(user.id) === rec) {
				console.log(`Home proxy: agent déconnecté (${who})`);
				this.dropUser(user.id, socket);
			}
		});
		socket.on("error", () => {
			if (this.byUser.get(user.id) === rec)
				this.dropUser(user.id, socket);
		});
	}

	approve(userId) {
		const rec = this.byUser.get(userId);
		if (!rec) return false;
		rec.approved = true;
		this.send(rec, encodeAgent(A.APPROVED, 0, 0, null));
		console.log(`Home proxy: approuvé user ${userId}`);
		return true;
	}

	reject(userId, stayDown = false) {
		const rec = this.byUser.get(userId);
		if (!rec) return false;
		rec.approved = false;
		this.send(rec, encodeAgent(A.REJECTED, 0, stayDown ? 1 : 0, null));
		console.log(`Home proxy: ${stayDown ? "déconnecté" : "refusé"} user ${userId}`);
		setTimeout(() => this.dropUser(userId), 150);
		return true;
	}
}

const homeAgents = new HomeAgents();

function json(res, status, body, extraHeaders) {
	const headers = {
		"Content-Type": "application/json; charset=utf-8",
		"Cache-Control": "no-store",
		"Cross-Origin-Resource-Policy": "same-origin",
		...(extraHeaders || {})
	};
	res.writeHead(status, headers);
	res.end(JSON.stringify(body));
}

function isCheckableIpv4(ip) {
	const m = String(ip || "").trim().match(/^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$/);
	if (!m) return false;
	const oct = m.slice(1).map(Number);
	if (oct.some((n) => n > 255)) return false;
	const [a, b] = oct;
	if (a === 0 || a === 127 || a >= 224) return false;
	if (a === 169 && b === 254) return false;
	return true;
}

function isPrivateIpv4(ip) {
	const m = String(ip || "").trim().match(/^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$/);
	if (!m) return false;
	const a = Number(m[1]);
	const b = Number(m[2]);
	if (a === 10) return true;
	if (a === 192 && b === 168) return true;
	if (a === 172 && b >= 16 && b <= 31) return true;
	return false;
}

function clientIpv4(req) {
	const hdrs = [
		req.headers["cf-connecting-ip"],
		req.headers["x-real-ip"],
		String(req.headers["x-forwarded-for"] || "").split(",")[0]
	];
	let raw = "";
	for (const h of hdrs) {
		const v = String(h || "").trim();
		if (v) {
			raw = v;
			break;
		}
	}
	if (!raw) raw = String(req.socket?.remoteAddress || "");
	const ip = raw.replace(/^\[|\]$/g, "").replace(/^::ffff:/i, "").trim();
	return isCheckableIpv4(ip) ? ip : "";
}

function isElectronReq(req) {
	return /\bElectron\b/i.test(String(req.headers["user-agent"] || ""));
}

function publicMeta(req, user) {
	const info = user && !isElectronReq(req)
		? homeAgents.info(user.id)
		: { homeProxy: false, homeProxyPending: false, homeProxyName: "" };
	const meta = store.meta(user);
	return {
		...meta,
		ipv4: clientIpv4(req),
		...info,
		discoveryEnabled: meta.discoveryEnabled !== false || !!info.homeProxy
	};
}

function checkTcpPort(host, port, ms) {
	return new Promise((resolve) => {
		const sock = net.connect({ host, port, family: 4 });
		const timer = setTimeout(() => {
			sock.destroy();
			resolve("filtered");
		}, ms);
		sock.once("connect", () => {
			clearTimeout(timer);
			sock.destroy();
			resolve("open");
		});
		sock.once("error", () => {
			clearTimeout(timer);
			resolve("closed");
		});
	});
}

const portCheckAt = new Map();

async function runPortCheck(host, user) {
	if (user && homeAgents.online(user.id))
		return homeAgents.portCheck(user.id, host, 9295, 3500);
	if (isPrivateIpv4(host)) {
		const info = user ? homeAgents.info(user.id) : { homeProxyPending: false };
		return {
			error: info.homeProxyPending ? "home_proxy_pending" : "need_home_proxy",
			ports: [],
			via: "wan"
		};
	}
	const status = await checkTcpPort(host, 9295, 2000);
	return { ports: [{ port: 9295, proto: "tcp", role: "session", status }], via: "wan" };
}

function readBody(req, limit = 1024 * 1024) {
	return new Promise((resolve, reject) => {
		const chunks = [];
		let size = 0;
		req.on("data", (c) => {
			size += c.length;
			if (size > limit) {
				reject(new Error("too large"));
				req.destroy();
				return;
			}
			chunks.push(c);
		});
		req.on("end", () => {
			const raw = Buffer.concat(chunks).toString("utf8");
			if (!raw) return resolve({});
			try { resolve(JSON.parse(raw)); }
			catch { reject(new Error("invalid json")); }
		});
		req.on("error", reject);
	});
}

function currentUser(req) {
	if (!cfg.authEnabled) return store.ensureLocalUser();
	return store.userBySession(readCookie(req, "chiaki_sid"));
}

function requireUser(req, res) {
	const user = currentUser(req);
	if (user) return user;
	json(res, 401, { error: "auth_required" });
	return null;
}

async function handleApi(req, res, reqUrl) {
	const secure = req.socket?.encrypted || req.headers["x-forwarded-proto"] === "https";
	const route = req.method + " " + reqUrl.pathname;

	if (route === "GET /api/meta") {
		json(res, 200, publicMeta(req, currentUser(req)));
		return true;
	}
	if (route === "POST /api/proxy/approve") {
		const user = requireUser(req, res);
		if (!user) return true;
		if (!homeAgents.approve(user.id)) {
			json(res, 404, { error: "no_agent" });
			return true;
		}
		json(res, 200, publicMeta(req, user));
		return true;
	}
	if (route === "POST /api/proxy/reject") {
		const user = requireUser(req, res);
		if (!user) return true;
		homeAgents.reject(user.id, false);
		json(res, 200, publicMeta(req, user));
		return true;
	}
	if (route === "POST /api/proxy/disconnect") {
		const user = requireUser(req, res);
		if (!user) return true;
		homeAgents.reject(user.id, true);
		json(res, 200, publicMeta(req, user));
		return true;
	}
	if (route === "GET /api/proxy-builds") {
		const dir = path.join(WWW, "downloads");
		const files = {
			windows: "chiaki-proxy-windows.exe",
			linux: "chiaki-proxy-linux",
			macos: "chiaki-proxy-macos"
		};
		const builds = {};
		for (const [k, name] of Object.entries(files)) {
			const fp = path.join(dir, name);
			builds[k] = fs.existsSync(fp) ? "/downloads/" + name : null;
		}
		json(res, 200, builds);
		return true;
	}
	if (route === "POST /api/login") {
		if (!cfg.authEnabled) {
			json(res, 400, { error: "auth_disabled" });
			return true;
		}
		let body;
		try { body = await readBody(req, 16 * 1024); }
		catch { json(res, 400, { error: "bad_request" }); return true; }
		const result = store.login(body.email, body.password);
		if (!result) {
			json(res, 401, { error: "invalid_credentials" });
			return true;
		}
		json(res, 200, publicMeta(req, result.user), {
			"Set-Cookie": sessionCookie(result.session.token, cfg.sessionDays * 86400, secure)
		});
		return true;
	}
	if (route === "POST /api/register") {
		if (!cfg.authEnabled || !cfg.allowRegister) {
			json(res, 403, { error: "register_disabled" });
			return true;
		}
		let body;
		try { body = await readBody(req, 16 * 1024); }
		catch { json(res, 400, { error: "bad_request" }); return true; }
		const result = store.register(body.username, body.email, body.password);
		if (result.error) {
			json(res, result.error === "taken" ? 409 : 400, { error: result.error });
			return true;
		}
		json(res, 200, publicMeta(req, result.user), {
			"Set-Cookie": sessionCookie(result.session.token, cfg.sessionDays * 86400, secure)
		});
		return true;
	}
	if (route === "POST /api/logout") {
		store.logout(readCookie(req, "chiaki_sid"));
		json(res, 200, { ok: true }, {
			"Set-Cookie": sessionCookie("", 0, secure)
		});
		return true;
	}
	if (route === "POST /api/account") {
		if (!cfg.authEnabled) {
			json(res, 400, { error: "auth_disabled" });
			return true;
		}
		const user = requireUser(req, res);
		if (!user) return true;
		let body;
		try { body = await readBody(req, 16 * 1024); }
		catch { json(res, 400, { error: "bad_request" }); return true; }
		const result = store.updateAccount(user.id, body);
		if (result.error) {
			const code = result.error === "invalid_credentials" ? 401
				: (result.error === "taken" || result.error === "email_taken") ? 409
				: result.error === "auth_required" ? 401
				: 400;
			json(res, code, { error: result.error });
			return true;
		}
		json(res, 200, publicMeta(req, result.user));
		return true;
	}
	if (route === "GET /api/share") {
		if (cfg.authEnabled) {
			const user = requireUser(req, res);
			if (!user) return true;
			json(res, 200, store.getShareByUser(user.id) || { active: false });
			return true;
		}
		json(res, 200, store.getShareByUser(store.ensureLocalUser().id) || { active: false });
		return true;
	}
	if (route === "POST /api/share") {
		const user = cfg.authEnabled ? requireUser(req, res) : store.ensureLocalUser();
		if (!user) return true;
		let body;
		try { body = await readBody(req, 16 * 1024); }
		catch { json(res, 400, { error: "bad_request" }); return true; }
		json(res, 200, store.saveShare(user.id, body, !!body.regenerate));
		return true;
	}
	if (route === "DELETE /api/share") {
		const user = cfg.authEnabled ? requireUser(req, res) : store.ensureLocalUser();
		if (!user) return true;
		store.deleteShare(user.id);
		json(res, 200, { ok: true });
		return true;
	}
	if (route === "GET /api/share/join") {
		const token = (reqUrl.searchParams.get("token") || "").trim();
		const share = store.getShareByToken(token);
		if (!share || !share.active) {
			json(res, 404, { error: "not_found" });
			return true;
		}
		json(res, 200, {
			video: share.video,
			audio: share.audio,
			vpad: share.vpad,
			gamepad: share.gamepad,
			vpadKeys: share.vpadKeys,
			language: store.shareLanguage(share.userId),
			viewers: shareHub.viewerCount(share.token)
		});
		return true;
	}
	if (route === "POST /api/account/delete") {
		if (!cfg.authEnabled) {
			json(res, 400, { error: "auth_disabled" });
			return true;
		}
		const user = requireUser(req, res);
		if (!user) return true;
		let body;
		try { body = await readBody(req, 16 * 1024); }
		catch { json(res, 400, { error: "bad_request" }); return true; }
		const result = store.deleteAccount(user.id, body.currentPassword);
		if (result.error) {
			json(res, result.error === "invalid_credentials" ? 401 : 400, { error: result.error });
			return true;
		}
		json(res, 200, { ok: true }, {
			"Set-Cookie": sessionCookie("", 0, secure)
		});
		return true;
	}
	if (route === "GET /api/state") {
		if (!cfg.authEnabled) {
			json(res, 400, { error: "auth_disabled" });
			return true;
		}
		const user = requireUser(req, res);
		if (!user) return true;
		json(res, 200, store.readProfile(user.id));
		return true;
	}
	if (route === "PUT /api/state") {
		if (!cfg.authEnabled) {
			json(res, 400, { error: "auth_disabled" });
			return true;
		}
		const user = requireUser(req, res);
		if (!user) return true;
		let body;
		try { body = await readBody(req, 2 * 1024 * 1024); }
		catch { json(res, 400, { error: "bad_request" }); return true; }
		json(res, 200, store.writeProfile(user.id, body));
		return true;
	}
	if (route === "POST /api/portcheck") {
		const user = requireUser(req, res);
		if (!user) return true;
		const now = Date.now();
		const prev = portCheckAt.get(user.id) || 0;
		if (now - prev < 2500) {
			json(res, 429, { error: "rate_limited" });
			return true;
		}
		let body;
		try { body = await readBody(req, 8 * 1024); }
		catch { json(res, 400, { error: "bad_request" }); return true; }
		const host = String(body.host || "").trim();
		if (!isCheckableIpv4(host)) {
			json(res, 400, { error: "invalid_host" });
			return true;
		}
		portCheckAt.set(user.id, now);
		const result = await runPortCheck(host, isElectronReq(req) ? null : user);
		const tcp = (result.ports || []).find((p) => p.port === 9295 && p.proto === "tcp");
		json(res, 200, {
			...result,
			ipv4: clientIpv4(req),
			ok: !!(tcp && tcp.status === "open")
		});
		return true;
	}
	if (route === "GET /api/speedtest") {
		const user = requireUser(req, res);
		if (!user) return true;
		const raw = Buffer.alloc(Math.min(512 * 1024, Math.max(64 * 1024, Number(reqUrl.searchParams.get("bytes")) || 384 * 1024)), 1);
		res.writeHead(200, {
			"Content-Type": "application/octet-stream",
			"Content-Length": String(raw.length),
			"Cache-Control": "no-store",
			"Cross-Origin-Resource-Policy": "same-origin"
		});
		res.end(raw);
		return true;
	}
	if (reqUrl.pathname.startsWith("/api/")) {
		json(res, 404, { error: "not_found" });
		return true;
	}
	return false;
}

function handleRequest(req, res) {
	if (req.url === "/health") {
		res.writeHead(200, { "Content-Type": "text/plain" });
		res.end("ok");
		return;
	}
	const reqUrl = new URL(req.url || "/", "http://127.0.0.1");
	handleApi(req, res, reqUrl).then((done) => {
		if (done) return;
		if (reqUrl.pathname === "/psn-account-id") {
			const username = (reqUrl.searchParams.get("username") || "").trim();
			if (!username) {
				res.writeHead(400, { "Content-Type": "application/json" });
				res.end(JSON.stringify({ error: "username required" }));
				return;
			}
			lookupPsnAccountId(username).then(({ status, body }) => {
				res.writeHead(body && body.encoded_id ? 200 : (status >= 400 ? status : 404), { "Content-Type": "application/json" });
				res.end(JSON.stringify(body || { error: "not found" }));
			}).catch((err) => {
				res.writeHead(502, { "Content-Type": "application/json" });
				res.end(JSON.stringify({ error: err.message || "lookup failed" }));
			});
			return;
		}
		let urlPath = decodeURIComponent(reqUrl.pathname);
		if (urlPath === "/") urlPath = "/index.html";
		if (urlPath === "/favicon.ico") urlPath = "/icons/chiaking-logo.svg";
		const file = resolvePublicFile(urlPath);
		if (!file) {
			res.writeHead(403);
			res.end("forbidden");
			return;
		}
		sendFile(req, res, file);
	}).catch((err) => {
		if (!res.headersSent) json(res, 500, { error: err.message || "server_error" });
	});
}

function isHomeAgentUpgrade(req) {
	const raw = String(req.url || "");
	const pathName = raw.split("?")[0];
	if (pathName === "/posix-agent" || pathName.startsWith("/posix-agent"))
		return true;
	if (pathName === "/posix-net/agent" || pathName.startsWith("/posix-net/agent"))
		return true;
	if (pathName === "/posix-net" || pathName.startsWith("/posix-net")) {
		try {
			return new URL(raw, "http://localhost").searchParams.get("agent") === "1";
		} catch {
			return /[?&]agent=1(?:&|$)/.test(raw);
		}
	}
	return false;
}

function completeWsUpgrade(req, socket) {
	const key = req.headers["sec-websocket-key"];
	if (!key) {
		socket.destroy();
		return false;
	}
	socket.write(
		"HTTP/1.1 101 Switching Protocols\r\n" +
		"Upgrade: websocket\r\n" +
		"Connection: Upgrade\r\n" +
		`Sec-WebSocket-Accept: ${wsAccept(key)}\r\n` +
		"Sec-WebSocket-Protocol: binary\r\n" +
		"\r\n"
	);
	socket.setNoDelay(true);
	return true;
}

function attachUpgrade(server) {
	server.on("upgrade", (req, socket) => {
		const pathName = String(req.url || "").split("?")[0];
		if (pathName.startsWith("/share-sig")) {
			shareHub.attach(req, socket);
			return;
		}
		if (isHomeAgentUpgrade(req)) {
			const user = cfg.authEnabled
				? store.userBySession(readCookie(req, "chiaki_sid"))
				: store.ensureLocalUser();
			if (!user) {
				socket.write("HTTP/1.1 401 Unauthorized\r\nConnection: close\r\n\r\n");
				socket.destroy();
				return;
			}
			if (!completeWsUpgrade(req, socket)) return;
			homeAgents.attach(req, socket, user);
			return;
		}
		if (!pathName.startsWith("/posix-net")) {
			socket.destroy();
			return;
		}
		if (cfg.authEnabled && !store.userBySession(readCookie(req, "chiaki_sid"))) {
			socket.write("HTTP/1.1 401 Unauthorized\r\nConnection: close\r\n\r\n");
			socket.destroy();
			return;
		}
		if (!completeWsUpgrade(req, socket)) return;
		const user = currentUser(req);
		if (user && !isElectronReq(req) && homeAgents.pipePosix(user, socket))
			return;
		let acc = Buffer.alloc(0);
		const emit = (payload) => {
			try { socket.write(encodeWsFrame(payload)); } catch {}
		};
		const bridge = new PosixBridge(emit);
		socket.on("data", (chunk) => {
			acc = Buffer.concat([acc, chunk]);
			for (;;) {
				const frame = decodeWsFrame(acc);
				if (!frame) break;
				acc = frame.rest;
				if (frame.opcode === 0x8) {
					bridge.closeAll();
					socket.end();
					return;
				}
				if (frame.opcode === 0x9) {
					const pong = Buffer.from(frame.payload);
					const hdr = Buffer.from([0x8a, pong.length]);
					socket.write(Buffer.concat([hdr, pong]));
					continue;
				}
				if (frame.opcode === 0x2 || frame.opcode === 0x1)
					bridge.handle(frame.payload);
			}
		});
		socket.on("close", () => bridge.closeAll());
		socket.on("error", () => bridge.closeAll());
	});
}

const httpServer = http.createServer(handleRequest);
attachUpgrade(httpServer);

const httpsServers = [];
let started = null;

function envFlag(name, fallback) {
	const v = process.env[name];
	if (v == null || v === "") return fallback;
	return /^(1|true|yes|on)$/i.test(String(v).trim());
}

function listenOnce(server, port, host) {
	return new Promise((resolve, reject) => {
		const onErr = (err) => {
			server.off("listening", onListen);
			reject(err);
		};
		const onListen = () => {
			server.off("error", onErr);
			resolve(server.address());
		};
		server.once("error", onErr);
		server.once("listening", onListen);
		server.listen(port, host);
	});
}

export async function startServers() {
	if (started) return started;
	const bindHost = process.env.CHIAKI_WASM_BIND || "127.0.0.1";
	const lanHttps = envFlag("CHIAKI_WASM_LAN_HTTPS", true);
	let wantPort = Number(process.env.CHIAKI_WASM_PORT || PORT || 8080);
	if (!Number.isFinite(wantPort) || wantPort < 0) wantPort = 8080;

	let addr;
	try {
		addr = await listenOnce(httpServer, wantPort, bindHost);
	} catch (err) {
		if (err && err.code === "EADDRINUSE" && wantPort !== 0) {
			console.warn(`Port ${wantPort} occupé, repli sur un port libre.`);
			addr = await listenOnce(httpServer, 0, bindHost);
		} else {
			throw err;
		}
	}
	const port = addr.port;
	const url = `http://${bindHost}:${port}/`;
	console.log(`chiaki-ng WASM (local): ${url}`);
	console.log(`POSIX proxy WS: ws://${bindHost}:${port}/posix-net`);
	console.log(`Home proxy agent: ws://${bindHost}:${port}/posix-net?agent=1`);
	console.log(`Fichiers UI: ${WWW}`);
	console.log(`Binaire WASM: ${ROOT}`);
	console.log(`SQLite: ${cfg.dbPath}`);
	console.log(`Auth: ${cfg.authEnabled ? "activée" : "désactivée"}`);
	console.log(`Découverte LAN: ${cfg.discoveryEnabled ? "activée" : "désactivée"}`);
	console.log(`Pause mot-clé: ${cfg.shareKeywordPause ? "activée" : "désactivée"}`);
	console.log("COOP/COEP activés (SharedArrayBuffer / pthreads).");

	if (lanHttps) {
		const tls = ensureTlsOptions();
		if (tls && tls.ips.length) {
			for (const ip of tls.ips) {
				const httpsServer = https.createServer({ key: tls.key, cert: tls.cert }, handleRequest);
				attachUpgrade(httpsServer);
				httpsServers.push(httpsServer);
				await listenOnce(httpsServer, port, ip);
				console.log(`chiaki-ng WASM (LAN HTTPS): https://${ip}:${port}/`);
				console.log("Acceptez l’avertissement de certificat, puis utilisez cette URL (HTTP LAN ne charge pas le WASM pthread).");
			}
		} else if (!tls) {
			console.log("LAN: WASM pthread exige HTTPS. Générez un certificat (openssl) ou ouvrez via http://127.0.0.1");
		}
	}

	started = {
		url,
		port,
		bindHost,
		async close() {
			const servers = [httpServer, ...httpsServers];
			await Promise.all(servers.map((s) => new Promise((resolve) => {
				if (!s.listening) return resolve();
				s.close(() => resolve());
			})));
			started = null;
		}
	};
	return started;
}

export { startServers as startServer };

function ranAsCli() {
	const entry = process.argv[1];
	if (!entry) return false;
	try {
		return path.resolve(fileURLToPath(import.meta.url)) === path.resolve(entry);
	} catch {
		return false;
	}
}

if (ranAsCli()) {
	startServers().catch((err) => {
		console.error(err);
		process.exit(1);
	});
}

