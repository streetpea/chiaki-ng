import crypto from "node:crypto";

function encodeWsText(text) {
	const payload = Buffer.from(String(text), "utf8");
	const len = payload.length;
	let header;
	if (len < 126) {
		header = Buffer.alloc(2);
		header[0] = 0x81;
		header[1] = len;
	} else if (len < 65536) {
		header = Buffer.alloc(4);
		header[0] = 0x81;
		header[1] = 126;
		header.writeUInt16BE(len, 2);
	} else {
		header = Buffer.alloc(10);
		header[0] = 0x81;
		header[1] = 127;
		header.writeBigUInt64BE(BigInt(len), 2);
	}
	return Buffer.concat([header, payload]);
}

export function createShareHub({ store, decodeWsFrame, wsAccept, readCookie }) {
	const rooms = new Map();

	function send(socket, obj) {
		try { socket.write(encodeWsText(JSON.stringify(obj))); }
		catch {}
	}

	function roomOf(token) {
		let room = rooms.get(token);
		if (!room) {
			room = { host: null, guests: new Map() };
			rooms.set(token, room);
		}
		return room;
	}

	function viewerCount(token) {
		const room = rooms.get(token);
		return room ? room.guests.size : 0;
	}

	function rightsOf(token) {
		return store.getShareByToken(token);
	}

	function dropSocket(socket) {
		const meta = socket._share;
		if (!meta) return;
		socket._share = null;
		const room = rooms.get(meta.token);
		if (!room) return;
		if (meta.role === "host" && room.host === socket) {
			room.host = null;
			for (const guest of room.guests.values())
				send(guest, { type: "host-left" });
		} else if (meta.role === "guest") {
			room.guests.delete(meta.id);
			if (room.host) send(room.host, { type: "guest-leave", id: meta.id, viewers: room.guests.size });
		}
		if (!room.host && room.guests.size === 0)
			rooms.delete(meta.token);
	}

	function onMessage(socket, raw) {
		const meta = socket._share;
		if (!meta) return;
		let msg;
		try { msg = JSON.parse(String(raw)); }
		catch { return; }
		const room = rooms.get(meta.token);
		if (!room) return;
		if (meta.role === "host") {
			if (msg.to && room.guests.has(msg.to)) {
				send(room.guests.get(msg.to), { ...msg, to: undefined, from: "host" });
				return;
			}
			if (msg.type === "status" || msg.type === "rights") {
				for (const guest of room.guests.values()) send(guest, msg);
			}
			return;
		}
		if (room.host) send(room.host, { ...msg, from: meta.id });
	}

	function attach(req, socket) {
		const url = new URL(req.url || "/", "http://127.0.0.1");
		const guestToken = (url.searchParams.get("token") || "").trim();
		let role = "host";
		let share = null;
		if (guestToken) {
			share = store.getShareByToken(guestToken);
			if (!share || !share.active) {
				socket.write("HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n");
				socket.destroy();
				return;
			}
			role = "guest";
		} else {
			const user = store.userBySession(readCookie(req, "chiaki_sid")) || store.ensureLocalUser();
			if (!user) {
				socket.write("HTTP/1.1 401 Unauthorized\r\nConnection: close\r\n\r\n");
				socket.destroy();
				return;
			}
			share = store.getShareByUser(user.id);
			if (!share || !share.active) {
				socket.write("HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n");
				socket.destroy();
				return;
			}
		}
		const key = req.headers["sec-websocket-key"];
		if (!key) {
			socket.destroy();
			return;
		}
		socket.write(
			"HTTP/1.1 101 Switching Protocols\r\n" +
			"Upgrade: websocket\r\n" +
			"Connection: Upgrade\r\n" +
			`Sec-WebSocket-Accept: ${wsAccept(key)}\r\n` +
			"\r\n"
		);
		const room = roomOf(share.token);
		const id = crypto.randomBytes(8).toString("hex");
		socket._share = { role, token: share.token, id };
		if (role === "host") {
			if (room.host && room.host !== socket) {
				try { room.host.end(); } catch {}
			}
			room.host = socket;
			send(socket, { type: "hello", role: "host", rights: share, viewers: room.guests.size });
			for (const [gid] of room.guests)
				send(socket, { type: "guest-join", id: gid, viewers: room.guests.size });
		} else {
			room.guests.set(id, socket);
			send(socket, {
				type: "hello",
				role: "guest",
				rights: share,
				language: store.shareLanguage(share.userId),
				viewers: room.guests.size
			});
			if (room.host) send(room.host, { type: "guest-join", id, viewers: room.guests.size });
		}

		let acc = Buffer.alloc(0);
		socket.on("data", (chunk) => {
			acc = Buffer.concat([acc, chunk]);
			for (;;) {
				const frame = decodeWsFrame(acc);
				if (!frame) break;
				acc = frame.rest;
				if (frame.opcode === 0x8) {
					dropSocket(socket);
					socket.end();
					return;
				}
				if (frame.opcode === 0x9) {
					const pong = Buffer.from(frame.payload);
					socket.write(Buffer.concat([Buffer.from([0x8a, pong.length]), pong]));
					continue;
				}
				if (frame.opcode === 0x1 || frame.opcode === 0x2)
					onMessage(socket, frame.payload);
			}
		});
		socket.on("close", () => dropSocket(socket));
		socket.on("error", () => dropSocket(socket));
	}

	return { attach, viewerCount, rightsOf };
}
