import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));

function parseEnv(text) {
	const out = {};
	for (const raw of String(text || "").split(/\r?\n/)) {
		const line = raw.trim();
		if (!line || line.startsWith("#")) continue;
		const eq = line.indexOf("=");
		if (eq < 1) continue;
		let key = line.slice(0, eq).trim();
		let val = line.slice(eq + 1).trim();
		if ((val.startsWith("\"") && val.endsWith("\"")) || (val.startsWith("'") && val.endsWith("'")))
			val = val.slice(1, -1);
		out[key] = val;
	}
	return out;
}

function bool(v, fallback = false) {
	if (v == null || v === "") return fallback;
	return /^(1|true|yes|on)$/i.test(String(v).trim());
}

function findEnvFile() {
	if (process.env.CHIAKI_ENV_FILE)
		return path.resolve(process.env.CHIAKI_ENV_FILE);
	const candidates = [
		path.resolve(process.cwd(), ".env"),
		path.resolve(here, "..", "..", ".env"),
		path.resolve(here, ".env")
	];
	return candidates.find((p) => fs.existsSync(p)) || candidates[1];
}

export function projectRoot() {
	return path.resolve(here, "..", "..");
}

export function loadEnv() {
	const file = findEnvFile();
	if (fs.existsSync(file)) {
		const parsed = parseEnv(fs.readFileSync(file, "utf8"));
		for (const [k, v] of Object.entries(parsed)) {
			if (process.env[k] == null || process.env[k] === "")
				process.env[k] = v;
		}
	}
	const root = projectRoot();
	const dbDir = process.env.CHIAKI_DB_DIR
		? path.resolve(process.env.CHIAKI_DB_DIR)
		: path.join(root, "db");
	const dbName = process.env.CHIAKI_DB_NAME || "chiaki.sqlite";
	return {
		envFile: file,
		root,
		port: Number(process.env.CHIAKI_WASM_PORT || 8080),
		authEnabled: bool(process.env.CHIAKI_AUTH_ENABLED, false),
		allowRegister: bool(process.env.CHIAKI_AUTH_ALLOW_REGISTER, true),
		sessionDays: Math.max(1, Number(process.env.CHIAKI_SESSION_DAYS || 30)),
		sessionSecret: process.env.CHIAKI_SESSION_SECRET || "",
		adminUser: (process.env.CHIAKI_ADMIN_USER || "").trim(),
		adminEmail: (process.env.CHIAKI_ADMIN_EMAIL || "").trim(),
		adminPassword: process.env.CHIAKI_ADMIN_PASSWORD || "",
		dbDir,
		dbName,
		dbPath: path.join(dbDir, dbName),
		maxHosts: Math.max(1, Number(process.env.CHIAKI_MAX_HOSTS || 32)),
		discoveryEnabled: bool(process.env.CHIAKI_DISCOVERY_ENABLED, true),
		shareKeywordPause: bool(process.env.CHIAKI_SHARE_KEYWORD_PAUSE, false)
	};
}
