import fs from "node:fs";
import crypto from "node:crypto";
import { DatabaseSync } from "node:sqlite";

const LOCAL_USER = "__local__";
const SCRYPT = { N: 16384, r: 8, p: 1, maxmem: 64 * 1024 * 1024 };

function hashPassword(password) {
	const salt = crypto.randomBytes(16);
	const hash = crypto.scryptSync(password, salt, 32, SCRYPT);
	return salt.toString("hex") + ":" + hash.toString("hex");
}

function verifyPassword(password, stored) {
	try {
		const [saltHex, hashHex] = String(stored || "").split(":");
		if (!saltHex || !hashHex) return false;
		const hash = crypto.scryptSync(password, Buffer.from(saltHex, "hex"), 32, SCRYPT);
		const expected = Buffer.from(hashHex, "hex");
		if (hash.length !== expected.length) return false;
		return crypto.timingSafeEqual(hash, expected);
	} catch {
		return false;
	}
}

function now() {
	return Date.now();
}

function parseJson(text, fallback) {
	try { return JSON.parse(text); }
	catch { return fallback; }
}

function normAddr(addr) {
	let s = String(addr || "").trim().toLowerCase();
	if (s.startsWith("[") && s.includes("]")) s = s.slice(1, s.indexOf("]"));
	s = s.replace(/^::ffff:/, "");
	if (/^\d{1,3}(?:\.\d{1,3}){3}/.test(s)) s = s.split(":")[0];
	return s;
}

function normHostId(id) {
	return String(id || "").toUpperCase().replace(/[^0-9A-F]/g, "");
}

function sanitizeVpadKeys(list) {
	if (!Array.isArray(list)) return null;
	const ok = new Set([
		"l2", "r2", "l1", "r1", "share", "options", "touchpad", "ls",
		"up", "left", "right", "down", "pyramid", "box", "moon", "cross",
		"rs", "ps", "l3", "r3"
	]);
	return list.map((id) => String(id)).filter((id) => ok.has(id)).slice(0, 24);
}

function claimKeysOf(h) {
	const keys = [];
	const id = normHostId(h && h.id);
	const addr = normAddr(h && (h.addr || h.host));
	if (id) keys.push("id:" + id);
	if (addr) keys.push("addr:" + addr);
	return keys;
}

function normalizeEmail(email) {
	return String(email || "").trim().toLowerCase();
}

function validEmail(email) {
	const s = normalizeEmail(email);
	return s.length >= 5 && s.length <= 128 && /^[^\s@]+@[^\s@]+$/.test(s);
}

function validUsername(name) {
	return /^[a-zA-Z0-9._-]{3,32}$/.test(name) && name !== LOCAL_USER;
}

function tableHasColumn(db, table, column) {
	return db.prepare(`PRAGMA table_info(${table})`).all().some((c) => c.name === column);
}

export function openStore(cfg) {
	fs.mkdirSync(cfg.dbDir, { recursive: true });
	const db = new DatabaseSync(cfg.dbPath);
	db.exec(`
		PRAGMA journal_mode = WAL;
		PRAGMA foreign_keys = ON;
		CREATE TABLE IF NOT EXISTS users (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			username TEXT NOT NULL UNIQUE COLLATE NOCASE,
			email TEXT,
			password_hash TEXT NOT NULL,
			created_at INTEGER NOT NULL
		);
		CREATE TABLE IF NOT EXISTS sessions (
			token TEXT PRIMARY KEY,
			user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
			expires_at INTEGER NOT NULL
		);
		CREATE TABLE IF NOT EXISTS profiles (
			user_id INTEGER PRIMARY KEY REFERENCES users(id) ON DELETE CASCADE,
			settings_json TEXT NOT NULL DEFAULT '{}',
			hosts_json TEXT NOT NULL DEFAULT '[]',
			hidden_json TEXT NOT NULL DEFAULT '[]',
			updated_at INTEGER NOT NULL DEFAULT 0
		);
		CREATE TABLE IF NOT EXISTS claimed_consoles (
			claim_key TEXT PRIMARY KEY,
			user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
			label TEXT NOT NULL DEFAULT '',
			addr TEXT NOT NULL DEFAULT '',
			host_id TEXT NOT NULL DEFAULT '',
			updated_at INTEGER NOT NULL
		);
		CREATE TABLE IF NOT EXISTS shares (
			user_id INTEGER PRIMARY KEY REFERENCES users(id) ON DELETE CASCADE,
			token TEXT NOT NULL UNIQUE,
			video INTEGER NOT NULL DEFAULT 1,
			audio INTEGER NOT NULL DEFAULT 1,
			vpad INTEGER NOT NULL DEFAULT 0,
			gamepad INTEGER NOT NULL DEFAULT 0,
			active INTEGER NOT NULL DEFAULT 0,
			created_at INTEGER NOT NULL
		);
	`);

	if (!tableHasColumn(db, "users", "email"))
		db.exec("ALTER TABLE users ADD COLUMN email TEXT");
	if (!tableHasColumn(db, "shares", "vpad_keys"))
		db.exec("ALTER TABLE shares ADD COLUMN vpad_keys TEXT");

	const userCols = "id, username, email, password_hash, created_at";
	const qUserByName = db.prepare(`SELECT ${userCols} FROM users WHERE username = ?`);
	const qUserByEmail = db.prepare(`SELECT ${userCols} FROM users WHERE email = ? COLLATE NOCASE`);
	const qUserById = db.prepare(`SELECT ${userCols} FROM users WHERE id = ?`);
	const qInsertUser = db.prepare("INSERT INTO users (username, email, password_hash, created_at) VALUES (?, ?, ?, ?)");
	const qUpdatePass = db.prepare("UPDATE users SET password_hash = ? WHERE id = ?");
	const qUpdateEmail = db.prepare("UPDATE users SET email = ? WHERE id = ?");
	const qUpdateAccount = db.prepare("UPDATE users SET username = ?, email = ?, password_hash = ? WHERE id = ?");
	const qNeedEmail = db.prepare("SELECT id, username FROM users WHERE (email IS NULL OR email = '') AND username != ?");
	const qProfile = db.prepare("SELECT settings_json, hosts_json, hidden_json, updated_at FROM profiles WHERE user_id = ?");
	const qUpsertProfile = db.prepare(`
		INSERT INTO profiles (user_id, settings_json, hosts_json, hidden_json, updated_at)
		VALUES (?, ?, ?, ?, ?)
		ON CONFLICT(user_id) DO UPDATE SET
			settings_json = excluded.settings_json,
			hosts_json = excluded.hosts_json,
			hidden_json = excluded.hidden_json,
			updated_at = excluded.updated_at
	`);
	const qInsertSession = db.prepare("INSERT INTO sessions (token, user_id, expires_at) VALUES (?, ?, ?)");
	const qSession = db.prepare("SELECT token, user_id, expires_at FROM sessions WHERE token = ?");
	const qDeleteSession = db.prepare("DELETE FROM sessions WHERE token = ?");
	const qDeleteSessionsByUser = db.prepare("DELETE FROM sessions WHERE user_id = ?");
	const qDeleteUser = db.prepare("DELETE FROM users WHERE id = ?");
	const qPurgeSessions = db.prepare("DELETE FROM sessions WHERE expires_at < ?");
	const qClaim = db.prepare("SELECT claim_key, user_id, label, addr, host_id FROM claimed_consoles WHERE claim_key = ?");
	const qDeleteClaimsByUser = db.prepare("DELETE FROM claimed_consoles WHERE user_id = ?");
	const qInsertClaim = db.prepare(`
		INSERT INTO claimed_consoles (claim_key, user_id, label, addr, host_id, updated_at)
		VALUES (?, ?, ?, ?, ?, ?)
		ON CONFLICT(claim_key) DO UPDATE SET
			label = excluded.label,
			addr = excluded.addr,
			host_id = excluded.host_id,
			updated_at = excluded.updated_at
		WHERE claimed_consoles.user_id = excluded.user_id
	`);
	const qForeignClaims = db.prepare(`
		SELECT claim_key, label, addr, host_id FROM claimed_consoles WHERE user_id != ?
	`);
	const qClaimCount = db.prepare("SELECT COUNT(*) AS n FROM claimed_consoles");
	const qAllProfiles = db.prepare("SELECT user_id, hosts_json FROM profiles");
	const shareCols = "user_id, token, video, audio, vpad, gamepad, vpad_keys, active, created_at";
	const qShareByUser = db.prepare(`SELECT ${shareCols} FROM shares WHERE user_id = ?`);
	const qShareByToken = db.prepare(`SELECT ${shareCols} FROM shares WHERE token = ?`);
	const qInsertShare = db.prepare(`
		INSERT INTO shares (user_id, token, video, audio, vpad, gamepad, vpad_keys, active, created_at)
		VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
	`);
	const qUpdateShare = db.prepare(`
		UPDATE shares SET token = ?, video = ?, audio = ?, vpad = ?, gamepad = ?, vpad_keys = ?, active = ? WHERE user_id = ?
	`);
	const qDeleteShare = db.prepare("DELETE FROM shares WHERE user_id = ?");

	function ensureProfile(userId) {
		if (!qProfile.get(userId))
			qUpsertProfile.run(userId, "{}", "[]", "[]", 0);
	}

	function createUser(username, password, email) {
		const info = qInsertUser.run(
			username,
			email ? normalizeEmail(email) : null,
			hashPassword(password),
			now()
		);
		ensureProfile(Number(info.lastInsertRowid));
		return Number(info.lastInsertRowid);
	}

	function ensureLocalUser() {
		let row = qUserByName.get(LOCAL_USER);
		if (!row) {
			createUser(LOCAL_USER, crypto.randomBytes(24).toString("hex"), null);
			row = qUserByName.get(LOCAL_USER);
		} else {
			ensureProfile(row.id);
		}
		return row;
	}

	for (const u of qNeedEmail.all(LOCAL_USER)) {
		try { qUpdateEmail.run(normalizeEmail(u.username + "@localhost"), u.id); }
		catch {}
	}

	if (cfg.adminUser && cfg.adminPassword) {
		const name = String(cfg.adminUser).trim();
		const email = normalizeEmail(
			cfg.adminEmail || (name.includes("@") ? name : name + "@localhost")
		);
		const username = name.includes("@")
			? name.slice(0, name.indexOf("@")).replace(/[^a-zA-Z0-9._-]/g, "").slice(0, 32) || "admin"
			: name;
		let existing = qUserByName.get(username);
		if (!existing && validEmail(email)) existing = qUserByEmail.get(email);
		if (!existing) createUser(username, cfg.adminPassword, email);
		else {
			qUpdatePass.run(hashPassword(cfg.adminPassword), existing.id);
			if (validEmail(email) && normalizeEmail(existing.email) !== email) {
				const taken = qUserByEmail.get(email);
				if (!taken || taken.id === existing.id)
					qUpdateEmail.run(email, existing.id);
			}
		}
	}
	ensureLocalUser();
	db.exec("UPDATE users SET email = NULL WHERE email = ''");
	db.exec("CREATE UNIQUE INDEX IF NOT EXISTS users_email_unique ON users(email COLLATE NOCASE)");

	function setClaims(userId, hosts) {
		qDeleteClaimsByUser.run(userId);
		const ts = now();
		for (const h of hosts || []) {
			const addr = normAddr(h.addr || h.host);
			const hostId = normHostId(h.id);
			const label = String(h.name || h.host || h.addr || "");
			for (const key of claimKeysOf(h))
				qInsertClaim.run(key, userId, label, addr, hostId, ts);
		}
	}

	function takeOwnHosts(userId, hosts) {
		const allowed = [];
		const rejected = [];
		for (const h of Array.isArray(hosts) ? hosts : []) {
			const foreign = claimKeysOf(h).some((key) => {
				const row = qClaim.get(key);
				return row && row.user_id !== userId;
			});
			if (foreign) rejected.push(h);
			else allowed.push(h);
		}
		return { allowed, rejected };
	}

	function listForeign(userId) {
		return qForeignClaims.all(userId).map((row) => ({
			key: row.claim_key,
			name: row.label,
			addr: row.addr,
			id: row.host_id
		}));
	}

	function rebuildClaimsIfEmpty() {
		if (Number(qClaimCount.get().n || 0) > 0) return;
		for (const row of qAllProfiles.all())
			setClaims(row.user_id, parseJson(row.hosts_json, []));
	}

	function publicUser(row) {
		if (!row || row.username === LOCAL_USER) return null;
		return {
			id: row.id,
			username: row.username,
			email: row.email || null,
			createdAt: Number(row.created_at || 0)
		};
	}

	function publicShare(row) {
		if (!row) return null;
		return {
			token: row.token,
			video: !!row.video,
			audio: !!row.audio,
			vpad: !!row.vpad,
			gamepad: !!row.gamepad,
			vpadKeys: sanitizeVpadKeys(parseJson(row.vpad_keys, null)),
			active: !!row.active
		};
	}

	function rightsFrom(body) {
		return {
			video: body.video !== false,
			audio: body.audio !== false,
			vpad: !!body.vpad,
			gamepad: !!body.gamepad,
			vpadKeys: sanitizeVpadKeys(body.vpadKeys),
			active: !!body.active
		};
	}

	function readProfile(userId) {
		ensureProfile(userId);
		rebuildClaimsIfEmpty();
		const row = qProfile.get(userId);
		return {
			settings: parseJson(row?.settings_json, {}),
			hosts: parseJson(row?.hosts_json, []),
			hidden: parseJson(row?.hidden_json, []),
			updatedAt: Number(row?.updated_at || 0)
		};
	}

	function writeProfile(userId, data) {
		const hosts = Array.isArray(data.hosts) ? data.hosts.slice(0, cfg.maxHosts) : [];
		const hidden = Array.isArray(data.hidden) ? data.hidden : [];
		const settings = data.settings && typeof data.settings === "object" ? data.settings : {};
		qUpsertProfile.run(
			userId,
			JSON.stringify(settings),
			JSON.stringify(hosts),
			JSON.stringify(hidden),
			now()
		);
		return readProfile(userId);
	}

	function createSession(userId) {
		qPurgeSessions.run(now());
		const token = crypto.randomBytes(32).toString("hex");
		const expires = now() + cfg.sessionDays * 86400000;
		qInsertSession.run(token, userId, expires);
		return { token, expires };
	}

	return {
		localUsername: LOCAL_USER,
		ensureLocalUser,
		meta(user) {
			return {
				authEnabled: cfg.authEnabled,
				allowRegister: cfg.allowRegister,
				maxHosts: cfg.maxHosts,
				discoveryEnabled: cfg.discoveryEnabled !== false,
				shareKeywordPause: cfg.shareKeywordPause === true,
				user: publicUser(user)
			};
		},
		userBySession(token) {
			if (!token) return null;
			const row = qSession.get(token);
			if (!row || row.expires_at < now()) {
				if (row) qDeleteSession.run(token);
				return null;
			}
			return qUserById.get(row.user_id) || null;
		},
		login(email, password) {
			const addr = normalizeEmail(email);
			if (!addr) return null;
			const row = qUserByEmail.get(addr);
			if (!row || row.username === LOCAL_USER) return null;
			if (!verifyPassword(password, row.password_hash)) return null;
			return { user: row, session: createSession(row.id) };
		},
		register(username, email, password) {
			const name = String(username || "").trim();
			const addr = normalizeEmail(email);
			if (!validUsername(name))
				return { error: "invalid_username" };
			if (!validEmail(addr))
				return { error: "invalid_email" };
			if (String(password || "").length < 6)
				return { error: "weak_password" };
			if (qUserByName.get(name))
				return { error: "taken" };
			if (qUserByEmail.get(addr))
				return { error: "email_taken" };
			const id = createUser(name, password, addr);
			const user = qUserById.get(id);
			return { user, session: createSession(id) };
		},
		updateAccount(userId, body) {
			const row = qUserById.get(userId);
			if (!row || row.username === LOCAL_USER)
				return { error: "auth_required" };
			const username = body.username != null ? String(body.username).trim() : row.username;
			const email = normalizeEmail(row.email);
			const newPassword = body.newPassword != null ? String(body.newPassword) : "";
			const currentPassword = String(body.currentPassword || "");
			if (!validUsername(username))
				return { error: "invalid_username" };
			if (email && !validEmail(email))
				return { error: "invalid_email" };
			if (!verifyPassword(currentPassword, row.password_hash))
				return { error: "invalid_credentials" };
			const passChanged = newPassword.length > 0;
			if (passChanged && newPassword.length < 6)
				return { error: "weak_password" };
			if (username !== row.username) {
				const other = qUserByName.get(username);
				if (other && other.id !== userId)
					return { error: "taken" };
			}
			qUpdateAccount.run(
				username,
				email || row.email,
				passChanged ? hashPassword(newPassword) : row.password_hash,
				userId
			);
			return { user: qUserById.get(userId) };
		},
		logout(token) {
			if (token) qDeleteSession.run(token);
		},
		deleteAccount(userId, password) {
			const row = qUserById.get(userId);
			if (!row || row.username === LOCAL_USER)
				return { error: "auth_required" };
			if (!verifyPassword(password, row.password_hash))
				return { error: "invalid_credentials" };
			qDeleteSessionsByUser.run(userId);
			qDeleteClaimsByUser.run(userId);
			qDeleteUser.run(userId);
			return { ok: true };
		},
		getShareByUser(userId) {
			return publicShare(qShareByUser.get(userId));
		},
		getShareByToken(token) {
			const row = qShareByToken.get(String(token || "").trim());
			return row ? { ...publicShare(row), userId: row.user_id } : null;
		},
		shareLanguage(userId) {
			if (!userId) return "en";
			const lang = String(readProfile(userId).settings?.language || "").toLowerCase();
			return lang.startsWith("fr") ? "fr" : "en";
		},
		saveShare(userId, body, regenerate) {
			const rights = rightsFrom(body || {});
			const row = qShareByUser.get(userId);
			const token = row && !regenerate ? row.token : crypto.randomBytes(16).toString("hex");
			const keysJson = rights.vpadKeys ? JSON.stringify(rights.vpadKeys) : null;
			if (!row)
				qInsertShare.run(userId, token, rights.video ? 1 : 0, rights.audio ? 1 : 0, rights.vpad ? 1 : 0, rights.gamepad ? 1 : 0, keysJson, rights.active ? 1 : 0, now());
			else
				qUpdateShare.run(token, rights.video ? 1 : 0, rights.audio ? 1 : 0, rights.vpad ? 1 : 0, rights.gamepad ? 1 : 0, keysJson, rights.active ? 1 : 0, userId);
			return publicShare(qShareByUser.get(userId));
		},
		deleteShare(userId) {
			qDeleteShare.run(userId);
		},
		readProfile,
		writeProfile
	};
}
