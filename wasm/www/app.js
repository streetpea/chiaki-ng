const $ = (id) => document.getElementById(id);

const BTN = {
	CROSS: 1 << 0, MOON: 1 << 1, BOX: 1 << 2, PYRAMID: 1 << 3,
	LEFT: 1 << 4, RIGHT: 1 << 5, UP: 1 << 6, DOWN: 1 << 7,
	L1: 1 << 8, R1: 1 << 9, L3: 1 << 10, R3: 1 << 11,
	OPTIONS: 1 << 12, SHARE: 1 << 13, TOUCHPAD: 1 << 14, PS: 1 << 15
};

const SETTINGS_KEY = "chiaki-wasm-settings";
const HOSTS_KEY = "chiaki-wasm-hosts";
const HIDDEN_KEY = "chiaki-wasm-hidden";
const REVEAL_ADDR_KEY = "chiaki-wasm-reveal-addrs";

const KEYMAP_ITEMS = [
	{ id: "cross", label: "Cross", kind: "button", bit: "CROSS", def: "Enter" },
	{ id: "moon", label: "Moon", kind: "button", bit: "MOON", def: "Backspace" },
	{ id: "box", label: "Box", kind: "button", bit: "BOX", def: "Backslash" },
	{ id: "pyramid", label: "Pyramid", kind: "button", bit: "PYRAMID", def: "KeyC" },
	{ id: "dpad_left", label: "D-Pad Left", kind: "button", bit: "LEFT", def: "ArrowLeft" },
	{ id: "dpad_right", label: "D-Pad Right", kind: "button", bit: "RIGHT", def: "ArrowRight" },
	{ id: "dpad_up", label: "D-Pad Up", kind: "button", bit: "UP", def: "ArrowUp" },
	{ id: "dpad_down", label: "D-Pad Down", kind: "button", bit: "DOWN", def: "ArrowDown" },
	{ id: "l1", label: "L1", kind: "button", bit: "L1", def: "Digit2" },
	{ id: "r1", label: "R1", kind: "button", bit: "R1", def: "Digit3" },
	{ id: "l3", label: "L3", kind: "button", bit: "L3", def: "Digit5" },
	{ id: "r3", label: "R3", kind: "button", bit: "R3", def: "Digit6" },
	{ id: "options", label: "Options", kind: "button", bit: "OPTIONS", def: "KeyO" },
	{ id: "share", label: "Share", kind: "button", bit: "SHARE", def: "KeyF" },
	{ id: "touchpad", label: "Touchpad", kind: "button", bit: "TOUCHPAD", def: "KeyT" },
	{ id: "ps", label: "PS", kind: "button", bit: "PS", def: "Escape" },
	{ id: "l2", label: "L2", kind: "trigger", axis: "l2", def: "Digit1" },
	{ id: "r2", label: "R2", kind: "trigger", axis: "r2", def: "Digit4" },
	{ id: "ls_right", label: "Left Stick Right", kind: "stick", axis: "lx", dir: 1, def: "BracketRight" },
	{ id: "ls_left", label: "Left Stick Left", kind: "stick", axis: "lx", dir: -1, def: "BracketLeft" },
	{ id: "ls_up", label: "Left Stick Up", kind: "stick", axis: "ly", dir: -1, def: "Insert" },
	{ id: "ls_down", label: "Left Stick Down", kind: "stick", axis: "ly", dir: 1, def: "Delete" },
	{ id: "rs_right", label: "Right Stick Right", kind: "stick", axis: "rx", dir: 1, def: "Equal" },
	{ id: "rs_left", label: "Right Stick Left", kind: "stick", axis: "rx", dir: -1, def: "Minus" },
	{ id: "rs_up", label: "Right Stick Up", kind: "stick", axis: "ry", dir: -1, def: "PageUp" },
	{ id: "rs_down", label: "Right Stick Down", kind: "stick", axis: "ry", dir: 1, def: "PageDown" }
];

const KEYMAP_GROUPS = [
	{ id: "face", keys: ["cross", "moon", "box", "pyramid"] },
	{ id: "dpad", keys: ["dpad_up", "dpad_down", "dpad_left", "dpad_right"] },
	{ id: "shoulders", keys: ["l1", "r1", "l2", "r2"] },
	{ id: "lstick", keys: ["ls_up", "ls_down", "ls_left", "ls_right", "l3"] },
	{ id: "rstick", keys: ["rs_up", "rs_down", "rs_left", "rs_right", "r3"] },
	{ id: "system", keys: ["options", "share", "touchpad", "ps"] }
];

const HOTKEY_ITEMS = [
	{ id: "hk_stop", def: [] },
	{ id: "hk_restart", def: [] },
	{ id: "hk_cursor", def: ["F8"] }
];

const defaultKeymap = Object.fromEntries([
	...KEYMAP_ITEMS.map((i) => [i.id, [i.def]]),
	...HOTKEY_ITEMS.map((i) => [i.id, i.def])
]);
const defaultMousemap = Object.fromEntries(
	[...KEYMAP_ITEMS, ...HOTKEY_ITEMS].map((i) => [i.id, []])
);
const MAX_BINDS = 8;

const CODE_LABELS = {
	Enter: "Return", NumpadEnter: "Return", Backspace: "Backspace", Backslash: "\\",
	Escape: "Esc", Space: "Space", Tab: "Tab", ShiftLeft: "Shift", ShiftRight: "Shift",
	ControlLeft: "Ctrl", ControlRight: "Ctrl", AltLeft: "Alt", AltRight: "Alt",
	ArrowLeft: "Left", ArrowRight: "Right", ArrowUp: "Up", ArrowDown: "Down",
	BracketLeft: "[", BracketRight: "]", Minus: "-", Equal: "=",
	Insert: "Insert", Delete: "Delete", PageUp: "PgUp", PageDown: "PgDown",
	Home: "Home", End: "End", CapsLock: "Caps Lock",
	Mouse0: "Left Click", Mouse1: "Middle Click", Mouse2: "Right Click",
	Mouse3: "Mouse 4", Mouse4: "Mouse 5",
	WheelUp: "Wheel Up", WheelDown: "Wheel Down"
};

function t(key, vars) {
	let s = (i18n && i18n[key]) || (i18nFallback && i18nFallback[key]) || key;
	if (vars) {
		for (const [k, v] of Object.entries(vars))
			s = s.replaceAll("{" + k + "}", v == null ? "" : String(v));
	}
	return s;
}

function detectLanguage() {
	return (navigator.language || "en").toLowerCase().startsWith("fr") ? "fr" : "en";
}

function shareLangCode(raw) {
	return String(raw || "").toLowerCase().startsWith("fr") ? "fr" : "en";
}

function uiLanguage() {
	if (share.isGuest && share.hostLang) return shareLangCode(share.hostLang);
	return settings.language === "fr" ? "fr" : "en";
}

function helpDocsUrl() {
	const lang = uiLanguage() === "en" ? "en" : "fr";
	return `doc/${lang}/index.html`;
}

function openHelpModal() {
	const modal = $("help-modal");
	const frame = $("help-frame");
	if (!modal || !frame) return;
	frame.src = helpDocsUrl();
	modal.classList.remove("hidden");
}

function closeHelpModal() {
	const modal = $("help-modal");
	const frame = $("help-frame");
	if (modal) modal.classList.add("hidden");
	if (frame) frame.src = "about:blank";
}

async function applyShareHostLanguage(raw) {
	const code = shareLangCode(raw);
	if (share.hostLang === code) return;
	share.hostLang = code;
	if (!share.isGuest) return;
	await loadI18n(code);
	applyI18n();
	refreshGuestOverlay();
}

async function loadI18n(lang) {
	const code = lang === "fr" ? "fr" : "en";
	const fetchJson = async (url) => {
		const ctrl = new AbortController();
		const timer = setTimeout(() => ctrl.abort(), 8000);
		try {
			const res = await fetch(url, { cache: "no-store", signal: ctrl.signal });
			return await res.json();
		} finally {
			clearTimeout(timer);
		}
	};
	if (!Object.keys(i18nFallback).length) {
		try { i18nFallback = await fetchJson("i18n/en.json"); }
		catch { i18nFallback = {}; }
	}
	if (code === "en") i18n = i18nFallback;
	else {
		try { i18n = await fetchJson("i18n/fr.json"); }
		catch { i18n = i18nFallback; }
	}
}

function applyI18n() {
	document.documentElement.lang = uiLanguage();
	document.querySelectorAll("[data-i18n]").forEach((el) => {
		el.textContent = t(el.dataset.i18n);
	});
	document.querySelectorAll("[data-i18n-title]").forEach((el) => {
		el.title = t(el.dataset.i18nTitle);
	});
	document.querySelectorAll("[data-i18n-placeholder]").forEach((el) => {
		el.placeholder = t(el.dataset.i18nPlaceholder);
	});
	renderKeymap();
	renderHosts();
	renderSavedList();
	applyVpadOpacity();
	refreshProxyStatus();
	syncHomeProxyUi();
	renderProxyDownloads();
	syncFullscreenButton();
	syncStopButton();
	syncDocumentTitle();
	updateShareBanners();
}

function bindingLabel(code) {
	if (!code) return "—";
	const translated = t("code." + code);
	if (translated !== "code." + code) return translated;
	if (CODE_LABELS[code]) return CODE_LABELS[code];
	if (code.startsWith("Key") && code.length === 4) return code.slice(3);
	if (code.startsWith("Digit")) return code.slice(5);
	if (code.startsWith("Numpad")) return "Num " + code.slice(6);
	if (code.startsWith("F") && /^F\d+$/.test(code)) return code;
	return code;
}

const defaultSettings = {
	disconnect: "2",
	suspend: "0",
	av: "0",
	decoder: "auto",
	window: "0",
	dblfs: false,
	preset: "2",
	console: "1",
	resolution: "4",
	fps: "60",
	bitrate: "15000",
	codec: "1",
	audiobuf: "60",
	psnId: "",
	keyboardEnabled: true,
	mouseTouchEnabled: true,
	mouseStick: "rs",
	mouseSens: 80,
	mouseInvertY: false,
	proxyUrl: "",
	vpadEnabled: false,
	vpadLayout: {},
	vpadOpacity: 55,
	vpadTheme: "classic",
	vpadCustom: {},
	language: "en",
	discoveryEnabled: true,
	keymap: { ...defaultKeymap },
	mousemap: { ...defaultMousemap },
	shareKeywordPause: false,
	shareKeywords: ""
};

let i18n = {};
let i18nFallback = {};

let api = {};
let wasmReadyP = null;
let vpadHeld = new Map();
let decoder = null;
let audio = { ctx: null, next: 0, channels: 2, rate: 48000 };
let streaming = false;
let codec = 1;
let configPending = [];
let decoderReady = false;
let decoderMime = "avc1.640028";
let decoderSetup = null;
let decoderFailed = false;
let gotKeyframe = false;
let lastIdrAt = 0;
let lastDecodeErrAt = 0;
let videoTs = 0;
let keys = new Set();
let mouseButtons = new Set();
let mouseWheel = "";
let mouseWheelTimer = 0;
let mouseLook = { x: 0, y: 0 };
let capturing = null;
let cursorLocked = true;
let hotkeyArmed = new Set();
let touch = { active: false, x: 960, y: 471 };
let discoveryOn = false;
let probeRunning = false;
let lastProbeKey = "";
let discoveryPaused = false;
let lastPadSent = "";
let discovered = [];
let discoveredSeenAt = new Map();
let hostsRenderTimer = 0;
let lastConsoleAdminSig = "";
let lastHostListSig = "";
let hostsPointerDown = false;
let discoveryRestartTimer = 0;
let currentView = "welcome";
let streamTitleName = "";
let selectedAddr = null;
let svgIcons = { ps4: "", ps5: "" };
let settings = { ...defaultSettings };
let proxyUrl = "";
let proxyState = "";
let cloud = { authEnabled: false, allowRegister: true, maxHosts: 32, discoveryEnabled: true, shareKeywordPause: false, user: null, ipv4: "", homeProxy: false, homeProxyPending: false, homeProxyName: "" };
let cloudPushTimer = 0;
let stateGen = 0;
let homeProxyWatch = 0;
let proxyBuilds = null;
let authUnlocked = null;
let settingsReturnView = "welcome";
let vpadOn = false;
let vpad = { buttons: 0, l2: 0, r2: 0, lx: 0, ly: 0, rx: 0, ry: 0 };
let vpadPtrs = new Map();
let connecting = false;
let connectSeq = 0;
let wakingAddrs = new Set();
let activeHost = null;
let appliedStreamKey = "";
let ignoreQuit = false;
let retryingConnect = false;
let sessionGate = null;
let sessionStopping = false;
let confirmDone = null;

const canvas = $("video");
const ctx2d = canvas.getContext("2d");

function log(msg, level) {
	const text = maskIpsInText(msg);
	const el = $("log");
	if (el) {
		el.textContent += `[${new Date().toLocaleTimeString()}] ${text}\n`;
		el.scrollTop = el.scrollHeight;
	}
	if (level === 0) console.error(text);
	else console.log(text);
}

function loadSettings() {
	let parsed = {};
	try { parsed = JSON.parse(localStorage.getItem(SETTINGS_KEY) || "{}"); }
	catch { parsed = {}; }
	settings = { ...defaultSettings, ...parsed };
	const maps = normalizeBindingMaps(parsed);
	settings.keymap = maps.keymap;
	settings.mousemap = maps.mousemap;
	if (!settings.vpadLayout || typeof settings.vpadLayout !== "object") settings.vpadLayout = {};
	if (!settings.vpadCustom || typeof settings.vpadCustom !== "object") settings.vpadCustom = {};
	if (!["classic", "dualsense", "xbox", "outline", "neon"].includes(settings.vpadTheme))
		settings.vpadTheme = "classic";
	settings.vpadOpacity = Math.max(1, Math.min(100, Number(settings.vpadOpacity) || 55));
	settings.mouseSens = clamp(Number(settings.mouseSens) || 80, 10, 200);
	if (!parsed.mouseStickDefaultRs) {
		if (!parsed.mouseStick || parsed.mouseStick === "none") settings.mouseStick = "rs";
		settings.mouseStickDefaultRs = true;
		localStorage.setItem(SETTINGS_KEY, JSON.stringify(settings));
	}
	if (!["none", "ls", "rs"].includes(settings.mouseStick)) settings.mouseStick = "rs";
	if (!parsed.codecDefaultH265) {
		settings.codec = "1";
		settings.codecDefaultH265 = true;
		localStorage.setItem(SETTINGS_KEY, JSON.stringify(settings));
	}
	if (!parsed.streamManual1080) {
		settings.resolution = "4";
		settings.fps = "60";
		settings.bitrate = "15000";
		delete settings.streamAuto;
		settings.streamManual1080 = true;
		localStorage.setItem(SETTINGS_KEY, JSON.stringify(settings));
	}
	if (!Number(settings.audiobuf)) settings.audiobuf = "60";
	if (!parsed.language) settings.language = detectLanguage();
	const map = {
		"s-disconnect": "disconnect", "s-suspend": "suspend", "s-av": "av",
		"s-decoder": "decoder", "s-window": "window", "s-preset": "preset",
		"s-console": "console", "s-resolution": "resolution", "s-fps": "fps",
		"s-bitrate": "bitrate", "s-codec": "codec", "s-audiobuf": "audiobuf",
		"s-psn-id": "psnId", "s-language": "language"
	};
	for (const [id, key] of Object.entries(map)) {
		const el = $(id);
		if (el) el.value = settings[key];
	}
	if ($("s-discovery")) $("s-discovery").value = settings.discoveryEnabled === false ? "false" : "true";
	$("s-dblfs").checked = !!settings.dblfs;
	$("s-keyboard").checked = settings.keyboardEnabled !== false;
	$("s-mouse").checked = settings.mouseTouchEnabled !== false;
	$("s-mouse-stick").value = settings.mouseStick || "rs";
	$("s-mouse-sens").value = String(settings.mouseSens || 80);
	$("s-mouse-invert").checked = !!settings.mouseInvertY;
	syncMouseSensLabel();
	applyVpadOpacity();
	applyDiscoveryUi();
	syncProxyUi();
}

function saveSettings() {
	settings = {
		...settings,
		disconnect: $("s-disconnect").value,
		suspend: $("s-suspend").value,
		av: $("s-av").value,
		decoder: $("s-decoder").value,
		window: $("s-window").value,
		dblfs: $("s-dblfs").checked,
		preset: $("s-preset").value,
		console: $("s-console").value,
		resolution: $("s-resolution").value,
		fps: $("s-fps").value,
		bitrate: $("s-bitrate").value,
		codec: $("s-codec").value,
		audiobuf: $("s-audiobuf").value,
		psnId: $("s-psn-id").value.trim(),
		keyboardEnabled: $("s-keyboard").checked,
		mouseTouchEnabled: $("s-mouse").checked,
		mouseStick: $("s-mouse-stick").value,
		mouseSens: clamp(Number($("s-mouse-sens").value) || 80, 10, 200),
		mouseInvertY: $("s-mouse-invert").checked,
		vpadOpacity: vpadOpacityPct(),
		vpadTheme: vpadThemeId(),
		vpadCustom: settings.vpadCustom && typeof settings.vpadCustom === "object" ? settings.vpadCustom : {},
		language: $("s-language").value,
		discoveryEnabled: $("s-discovery")?.value !== "false",
		shareKeywordPause: $("share-opt-keyword-pause") ? $("share-opt-keyword-pause").checked : !!settings.shareKeywordPause,
		shareKeywords: $("share-keywords") ? $("share-keywords").value : (settings.shareKeywords || ""),
		keymap: { ...defaultKeymap, ...(settings.keymap || {}) },
		mousemap: { ...defaultMousemap, ...(settings.mousemap || {}) }
	};
	localStorage.setItem(SETTINGS_KEY, JSON.stringify(settings));
	stateGen++;
	scheduleCloudPush();
}

function streamConfigKey() {
	return [
		settings.codec, settings.resolution, settings.fps,
		settings.bitrate, settings.console, settings.decoder
	].join("|");
}

async function applySettingsNow() {
	const langBefore = settings.language;
	const discoveryBefore = settings.discoveryEnabled !== false;
	saveSettings();
	applyVpadOpacity();
	syncMouseSensLabel();
	syncStreamChrome();
	if (streaming) pushAudioCfg();
	applyDiscoveryUi();
	if ((settings.discoveryEnabled !== false) && !discoveryBefore) startDiscovery();
	if (settings.language !== langBefore) {
		await loadI18n(settings.language);
		applyI18n();
		if (share.active && !share.isGuest) shareBroadcastState();
	}
}

function isMouseCode(code) {
	return typeof code === "string" && /^(Mouse\d+|WheelUp|WheelDown)$/.test(code);
}

function syncMouseSensLabel() {
	const out = $("s-mouse-sens-val");
	if (out) out.textContent = String(settings.mouseSens || 80);
}

function asCodeList(value) {
	if (Array.isArray(value)) return value.filter((c) => typeof c === "string" && c);
	if (typeof value === "string" && value) return [value];
	return [];
}

function normalizeBindingMaps(parsed) {
	const rawKb = (parsed && parsed.keymap) || {};
	const rawMs = (parsed && parsed.mousemap) || {};
	const hasKb = parsed && parsed.keymap && typeof parsed.keymap === "object";
	const keymap = {};
	const mousemap = {};
	for (const item of [...KEYMAP_ITEMS, ...HOTKEY_ITEMS]) {
		let kb = asCodeList(hasKb && Object.prototype.hasOwnProperty.call(rawKb, item.id)
			? rawKb[item.id]
			: defaultKeymap[item.id]);
		let ms = asCodeList(rawMs[item.id]);
		const moved = kb.filter(isMouseCode);
		kb = kb.filter((c) => !isMouseCode(c));
		for (const code of moved) {
			if (!ms.includes(code)) ms.push(code);
		}
		keymap[item.id] = kb;
		mousemap[item.id] = ms;
	}
	return { keymap, mousemap };
}

function codesOf(id, mouse) {
	const src = mouse ? settings.mousemap : settings.keymap;
	return asCodeList(src && src[id]);
}

function setCodes(id, mouse, list) {
	const unique = [];
	for (const code of list) {
		if (code && !unique.includes(code)) unique.push(code);
	}
	if (mouse) settings.mousemap = { ...(settings.mousemap || {}), [id]: unique };
	else settings.keymap = { ...(settings.keymap || {}), [id]: unique };
}

function bindChip(code, kind) {
	const safe = escapeHtml(code);
	return `<span class="bind-chip bind-${kind}" data-code="${safe}">${escapeHtml(bindingLabel(code))}<span class="chip-x" data-remove="${safe}" role="button">×</span></span>`;
}

function chipsHtml(id) {
	const kb = codesOf(id, false).map((c) => bindChip(c, "kb")).join("");
	const ms = codesOf(id, true).map((c) => bindChip(c, "ms")).join("");
	return kb + ms || `<span class="bind-chip empty">—</span>`;
}

function appendBindRow(body, id) {
	const empty = !codesOf(id, false).length && !codesOf(id, true).length;
	const listen = capturing && capturing.id === id ? " listening" : "";
	const row = document.createElement("tr");
	row.innerHTML = `<td class="bind-action">${t("key." + id)}</td><td><button type="button" class="bind-btn${listen}${empty ? " empty" : ""}" data-key-id="${id}"><span class="bind-chips">${chipsHtml(id)}</span></button></td>`;
	const btn = row.querySelector(".bind-btn");
	btn.onclick = (e) => {
		const rm = e.target.closest("[data-remove]");
		if (rm) {
			e.preventDefault();
			e.stopPropagation();
			removeBinding(id, rm.dataset.remove);
			return;
		}
		startKeyCapture({ id });
	};
	body.appendChild(row);
}

function renderHotkeys() {
	const body = $("hotkeys-body");
	if (!body) return;
	body.innerHTML = "";
	for (const item of HOTKEY_ITEMS) appendBindRow(body, item.id);
}

function renderKeymap() {
	const body = $("keys-table-body");
	if (!body) return;
	body.innerHTML = "";
	for (const group of KEYMAP_GROUPS) {
		const head = document.createElement("tr");
		head.className = "group-row";
		head.innerHTML = `<td colspan="2">${t("keys.group." + group.id)}</td>`;
		body.appendChild(head);
		for (const id of group.keys) appendBindRow(body, id);
	}
	renderHotkeys();
	syncCaptureSlots();
}

function fillSlot(el, id, mouse) {
	if (!el) return;
	const list = codesOf(id, mouse);
	el.innerHTML = list.length
		? list.map((c) => bindChip(c, mouse ? "ms" : "kb")).join("")
		: `<span class="bind-chip empty">—</span>`;
	el.querySelectorAll("[data-remove]").forEach((btn) => {
		btn.onclick = (e) => {
			e.preventDefault();
			e.stopPropagation();
			removeBinding(id, btn.dataset.remove);
		};
	});
}

function syncCaptureSlots() {
	if (!capturing) return;
	fillSlot($("keycap-kb"), capturing.id, false);
	fillSlot($("keycap-ms"), capturing.id, true);
}

function startKeyCapture(item) {
	capturing = { id: item.id };
	$("keycap-target").textContent = t("key." + item.id);
	const help = document.querySelector("#keycap-modal [data-i18n='keycap.help']");
	if (help) help.textContent = t("keycap.help");
	$("keycap-modal").classList.remove("hidden");
	syncCaptureSlots();
	renderKeymap();
}

function clearBinding(id) {
	setCodes(id, false, []);
	setCodes(id, true, []);
	saveSettings();
	if (capturing && capturing.id === id) {
		syncCaptureSlots();
		renderKeymap();
		return;
	}
	capturing = null;
	$("keycap-modal").classList.add("hidden");
	renderKeymap();
}

function removeBinding(id, code) {
	if (!id || !code) return;
	const mouse = isMouseCode(code);
	setCodes(id, mouse, codesOf(id, mouse).filter((c) => c !== code));
	saveSettings();
	syncCaptureSlots();
	renderKeymap();
}

function applyCapturedCode(code) {
	if (!capturing || !code) return;
	if (code === "Escape") return;
	const mouse = isMouseCode(code);
	const list = codesOf(capturing.id, mouse);
	if (list.includes(code) || list.length >= MAX_BINDS) {
		syncCaptureSlots();
		return;
	}
	setCodes(capturing.id, mouse, [...list, code]);
	saveSettings();
	syncCaptureSlots();
	renderKeymap();
}

function finishKeyCapture(code) {
	if (!capturing) return;
	if (code) applyCapturedCode(code);
	capturing = null;
	$("keycap-modal").classList.add("hidden");
	renderKeymap();
}

function bindingHeld(code) {
	if (!code) return false;
	if (code === "WheelUp" || code === "WheelDown") return mouseWheel === code;
	if (code.startsWith("Mouse")) return mouseButtons.has(Number(code.slice(5)));
	return keys.has(code);
}

function mouse0Mapped() {
	return KEYMAP_ITEMS.some((item) => codesOf(item.id, true).includes("Mouse0"));
}

function findHotkey(code) {
	if (!code) return null;
	for (const item of HOTKEY_ITEMS) {
		if (codesOf(item.id, false).includes(code) || codesOf(item.id, true).includes(code))
			return item.id;
	}
	return null;
}

function wantsPointerLock() {
	return streaming && cursorLocked && !vpadOn
		&& $("stream-view") && !$("stream-view").classList.contains("hidden");
}

function syncPointerLock() {
	const want = wantsPointerLock();
	canvas.classList.toggle("cursor-hidden", want);
	if (!want && document.pointerLockElement) document.exitPointerLock?.();
}

function tryPointerLock() {
	if (!wantsPointerLock()) return;
	if (document.pointerLockElement !== canvas) canvas.requestPointerLock?.();
}

function toggleCursorLock() {
	if (vpadOn) {
		cursorLocked = false;
		syncPointerLock();
		return;
	}
	cursorLocked = !cursorLocked;
	syncPointerLock();
	if (cursorLocked) tryPointerLock();
}

function runHotkey(id) {
	if (id === "hk_stop") requestDisconnect();
	else if (id === "hk_restart") restartActiveStream();
	else if (id === "hk_cursor") toggleCursorLock();
}

function fireHotkey(code) {
	const id = findHotkey(code);
	if (!id || hotkeyArmed.has(id)) return false;
	hotkeyArmed.add(id);
	runHotkey(id);
	return true;
}

function releaseHotkey(code) {
	const id = findHotkey(code);
	if (id) hotkeyArmed.delete(id);
}

function savedHosts() {
	try { return JSON.parse(localStorage.getItem(HOSTS_KEY) || "[]"); }
	catch { return []; }
}
function persistHosts(list) {
	stateGen++;
	localStorage.setItem(HOSTS_KEY, JSON.stringify(list.slice(0, cloud.maxHosts || 32)));
	renderSavedList();
	renderHosts();
	scheduleCloudPush();
	syncDiscoveryService();
}
function hiddenSet() {
	try { return new Set(JSON.parse(localStorage.getItem(HIDDEN_KEY) || "[]")); }
	catch { return new Set(); }
}
function persistHidden(set) {
	stateGen++;
	localStorage.setItem(HIDDEN_KEY, JSON.stringify([...set]));
	scheduleCloudPush();
	renderHosts();
	renderSavedList();
}
function revealedAddrSet() {
	try { return new Set(JSON.parse(localStorage.getItem(REVEAL_ADDR_KEY) || "[]")); }
	catch { return new Set(); }
}
function persistRevealedAddrs(set) {
	localStorage.setItem(REVEAL_ADDR_KEY, JSON.stringify([...set]));
}
function toggleRevealAddr(addr) {
	const set = revealedAddrSet();
	if (set.has(addr)) set.delete(addr);
	else set.add(addr);
	persistRevealedAddrs(set);
}
function maskHostAddr(addr) {
	const raw = String(addr || "").trim();
	if (!raw) return "";
	const v4 = raw.match(/^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})(:\d+)?$/);
	if (v4) return `•••.•••.•••.•••${v4[5] || ""}`;
	if (raw.includes(":")) return "••••:••••:••••:••••";
	if (raw.length <= 5) return "••••";
	return "••••";
}

function maskIpsInText(text) {
	return String(text ?? "")
		.replace(/\b(?:\d{1,3}\.){3}\d{1,3}\b/g, "•••.•••.•••.•••")
		.replace(/\b(?:[0-9a-fA-F]{1,4}:){3,}[0-9a-fA-F]{1,4}\b/g, "••••:••••")
		.replace(/\b[0-9a-fA-F]{0,4}(?::[0-9a-fA-F]{1,4}){0,5}::[0-9a-fA-F:]*/g, "••••:••••");
}

async function cloudRequest(url, opts = {}) {
	const { timeoutMs = 15000, headers, signal, ...rest } = opts;
	const ctrl = new AbortController();
	const timer = setTimeout(() => ctrl.abort(), timeoutMs);
	if (signal) {
		if (signal.aborted) ctrl.abort();
		else signal.addEventListener("abort", () => ctrl.abort(), { once: true });
	}
	try {
		const res = await fetch(url, {
			credentials: "include",
			cache: "no-store",
			...rest,
			signal: ctrl.signal,
			headers: { "Content-Type": "application/json", ...(headers || {}) }
		});
		let body = {};
		try { body = await res.json(); } catch { body = {}; }
		return { ok: res.ok, status: res.status, body };
	} catch {
		return { ok: false, status: 0, body: {} };
	} finally {
		clearTimeout(timer);
	}
}

function scheduleCloudPush() {
	if (!cloud.authEnabled) return;
	clearTimeout(cloudPushTimer);
	cloudPushTimer = setTimeout(() => { pushCloudState().catch(() => {}); }, 400);
}

function snapshotState() {
	return {
		settings,
		hosts: savedHosts(),
		hidden: [...hiddenSet()]
	};
}

async function pushCloudState() {
	if (!cloud.authEnabled) return true;
	const gen = stateGen;
	const { ok, body } = await cloudRequest("/api/state", {
		method: "PUT",
		body: JSON.stringify(snapshotState())
	});
	if (!ok) return false;
	if (gen !== stateGen) return true;
	if (body && Array.isArray(body.hosts))
		localStorage.setItem(HOSTS_KEY, JSON.stringify(body.hosts));
	return true;
}

function applyRemoteState(data) {
	if (data.settings && typeof data.settings === "object") {
		localStorage.setItem(SETTINGS_KEY, JSON.stringify({ ...defaultSettings, ...data.settings }));
		loadSettings();
		applyDiscoveryUi();
	}
	if (Array.isArray(data.hosts))
		localStorage.setItem(HOSTS_KEY, JSON.stringify(data.hosts));
	if (Array.isArray(data.hidden))
		localStorage.setItem(HIDDEN_KEY, JSON.stringify(data.hidden));
	renderSavedList();
	renderHosts();
	syncDiscoveryService();
}

function clearLocalAccountData() {
	localStorage.removeItem(HOSTS_KEY);
	localStorage.removeItem(HIDDEN_KEY);
	localStorage.removeItem(SETTINGS_KEY);
}

function closeAccountMenu() {
	const menu = $("account-menu");
	const btn = $("btn-account");
	if (menu) menu.classList.add("hidden");
	if (btn) btn.setAttribute("aria-expanded", "false");
}

function refreshAuthUi() {
	const who = $("auth-who");
	const account = $("tb-account");
	const screen = $("auth-screen");
	const name = cloud.user && cloud.user.username;
	if (who) who.textContent = name || "";
	if (account) account.classList.toggle("hidden", !cloud.authEnabled || !name);
	if (!name) closeAccountMenu();
	if (screen) screen.classList.add("hidden");
	const toRegister = $("auth-to-register");
	if (toRegister) toRegister.classList.toggle("hidden", !cloud.allowRegister);
	if (!cloud.allowRegister) showAuthPanel("login");
}

function showAccountError(key, ok) {
	const el = $("account-error");
	if (!el) return;
	el.textContent = key ? t(key) : "";
	el.classList.toggle("hidden", !key);
	el.classList.toggle("ok", !!ok);
}

function formatAccountDate(ts) {
	if (!ts) return "—";
	const locale = settings.language === "fr" ? "fr-FR" : "en-US";
	return new Date(Number(ts)).toLocaleString(locale);
}

function closeAccountSettings() {
	closeDeleteAccount();
	$("account-modal")?.classList.add("hidden");
}

function showDeleteAccountError(key) {
	const el = $("delete-account-error");
	if (!el) return;
	el.textContent = key ? t(key) : "";
	el.classList.toggle("hidden", !key);
}

function closeDeleteAccount() {
	$("delete-account-modal")?.classList.add("hidden");
	showDeleteAccountError("");
}

function openDeleteAccount() {
	const user = cloud.user || {};
	if ($("del-user")) $("del-user").textContent = user.username || "—";
	if ($("del-email")) $("del-email").textContent = user.email || "—";
	if ($("acc-delete-pass")) $("acc-delete-pass").value = "";
	showDeleteAccountError("");
	$("delete-account-modal")?.classList.remove("hidden");
	$("acc-delete-pass")?.focus();
}

function openAccountSettings() {
	closeAccountMenu();
	const user = cloud.user || {};
	if ($("acc-user")) $("acc-user").value = user.username || "";
	if ($("acc-email")) $("acc-email").textContent = user.email || "—";
	if ($("acc-created")) $("acc-created").textContent = formatAccountDate(user.createdAt);
	if ($("acc-current")) $("acc-current").value = "";
	if ($("acc-new")) $("acc-new").value = "";
	if ($("acc-confirm")) $("acc-confirm").value = "";
	showAccountError("");
	$("account-modal")?.classList.remove("hidden");
}

function showAuthPanel(mode) {
	const register = mode === "register";
	$("auth-login-form")?.classList.toggle("hidden", register);
	$("auth-register-form")?.classList.toggle("hidden", !register);
	showAuthError("");
}

function showAuthError(key) {
	const el = $("auth-error");
	if (!el) return;
	el.textContent = key ? t(key) : "";
	el.classList.toggle("hidden", !key);
}

function unlockAuth() {
	if (authUnlocked) {
		authUnlocked();
		authUnlocked = null;
	}
}

function isElectronApp() {
	return /\bElectron\b/i.test(navigator.userAgent || "");
}

function homeProxyUiEnabled() {
	return !isElectronApp();
}

const SHARE_ICE = { iceServers: [{ urls: "stun:stun.l.google.com:19302" }] };
const share = {
	isGuest: false,
	token: "",
	active: false,
	video: true,
	audio: true,
	vpad: false,
	gamepad: false,
	viewers: 0,
	hostStreaming: false,
	ws: null,
	pcs: new Map(),
	iceWait: new Map(),
	guestPads: new Map(),
	pendingGuests: new Set(),
	vpadTheme: "",
	vpadCustom: {},
	vpadLayout: {},
	vpadOpacity: 55,
	vpadKeys: null,
	hostLang: "",
	media: null,
	audioDest: null,
	bannerDismissed: false,
	capCanvas: null,
	capCtx: null,
	keepAlive: false,
	keyTimer: null,
	lastShareDraw: 0,
	paused: false,
	keywordWatch: false,
	ocrHay: "",
	ocrErr: false,
	ocrBusy: false,
	ocrAt: 0,
	ocrSnapAt: 0,
	ocrSnapReady: false,
	ocrMisses: 0,
	ocrClearUntil: 0,
	ocrPendingSnapAt: 0,
	ocrMissStreak: 0,
	ocrHitStreak: 0,
	lastHitKey: "",
	lastHitAt: 0,
	pauseAt: 0,
	ocrTimer: 0,
	ocrBusyTimer: 0,
	stageCanvas: null,
	stageCtx: null,
	delayRing: null,
	delayI: 0,
	delayAt: 0,
	delayShownT: 0,
	audioDelay: null,
	ocrPort: null,
	ocrTess: null,
	ocrTessP: null,
	ocrScriptP: null,
	blankCanvas: null,
	blankTrack: null
};

function shareTokenFromHash() {
	const m = String(location.hash || "").match(/[#&]share=([^&]+)/);
	return m ? decodeURIComponent(m[1]) : "";
}

function shareLinkUrl() {
	return share.token ? `${location.origin}/#share=${share.token}` : "";
}

function shareWsUrl(token) {
	const proto = location.protocol === "https:" ? "wss" : "ws";
	return token
		? `${proto}://${location.host}/share-sig?token=${encodeURIComponent(token)}`
		: `${proto}://${location.host}/share-sig`;
}

function shareSend(obj) {
	if (share.ws && share.ws.readyState === 1)
		share.ws.send(JSON.stringify(obj));
}

function shareRightsFromForm() {
	return {
		video: $("share-opt-video")?.checked !== false,
		audio: $("share-opt-audio")?.checked !== false,
		vpad: !!$("share-opt-vpad")?.checked,
		vpadKeys: shareVpadKeysFromForm(),
		gamepad: !!$("share-opt-gamepad")?.checked
	};
}

function applyShareForm(data) {
	if ($("share-opt-video")) $("share-opt-video").checked = data.video !== false;
	if ($("share-opt-audio")) $("share-opt-audio").checked = data.audio !== false;
	if ($("share-opt-vpad")) $("share-opt-vpad").checked = data.vpad !== false;
	if ($("share-opt-gamepad")) $("share-opt-gamepad").checked = !!data.gamepad;
	if ($("share-opt-keyword-pause")) $("share-opt-keyword-pause").checked = !!settings.shareKeywordPause;
	if ($("share-keywords")) $("share-keywords").value = settings.shareKeywords || "";
	updateShareKeywordStatus();
	syncShareKeywordUi();
	applyShareVpadKeysForm(data.vpadKeys);
	syncShareVpadKeysPanel();
}

function allVpadKeyIds() {
	return VPAD_ITEMS.map((item) => item.id);
}

function normalizeVpadKeys(list) {
	if (!Array.isArray(list)) return null;
	const valid = new Set(allVpadKeyIds());
	return list.map((id) => String(id)).filter((id) => valid.has(id));
}

function vpadKeyAllowed(id) {
	if (!share.isGuest) return true;
	if (!Array.isArray(share.vpadKeys)) return true;
	return share.vpadKeys.includes(id);
}

function shareVpadKeysFromForm() {
	const boxes = document.querySelectorAll("#share-vpad-keys-list input[data-vpad-key]");
	if (!boxes.length) return allVpadKeyIds();
	return [...boxes].filter((el) => el.checked).map((el) => el.dataset.vpadKey);
}

function applyShareVpadKeysForm(list) {
	const allow = Array.isArray(list) ? new Set(list) : null;
	document.querySelectorAll("#share-vpad-keys-list input[data-vpad-key]").forEach((el) => {
		el.checked = allow ? allow.has(el.dataset.vpadKey) : true;
	});
	syncShareVpadGroupChecks();
}

function syncShareVpadKeysPanel() {
	const on = !!$("share-opt-vpad")?.checked || !!$("share-opt-gamepad")?.checked;
	$("share-vpad-keys")?.classList.toggle("hidden", !on);
}

function foldShareText(s) {
	return String(s || "")
		.toLowerCase()
		.normalize("NFD")
		.replace(/[\u0300-\u036f]/g, "")
		.replace(/0/g, "o")
		.replace(/1/g, "l")
		.replace(/5/g, "s")
		.replace(/[^a-z0-9]+/g, " ")
		.trim();
}

function parseShareKeywords() {
	return String(settings.shareKeywords || "")
		.split(/[\n,;]+/)
		.map((s) => foldShareText(s))
		.filter((s) => s.length >= 3);
}

function shareIlFold(s) {
	return String(s || "").replace(/l/g, "i");
}

function shareOcrConfuseOk(a, b) {
	if (a === b) return true;
	const pairs = "il lo od ce ea bh uv mn ft pr";
	return pairs.includes(a + b) || pairs.includes(b + a);
}

function shareWordFitsKey(word, key) {
	if (!word || !key) return false;
	if (word === key) return true;
	if (word.length === key.length && shareIlFold(word) === shareIlFold(key)) return true;
	if (word.length === key.length && key.length >= 4) {
		let diff = 0;
		for (let i = 0; i < key.length; i++) {
			if (word[i] === key[i]) continue;
			diff++;
			if (diff > 1 || !shareOcrConfuseOk(word[i], key[i])) return false;
		}
		return diff <= 1;
	}
	if (key.length < 6) return false;
	if (Math.abs(word.length - key.length) > 1) return false;
	return shareEditDist(word, key, 1) <= 1;
}

function shareEditDist(a, b, max) {
	if (Math.abs(a.length - b.length) > max) return max + 1;
	const m = a.length;
	const n = b.length;
	let prev = new Array(n + 1);
	let cur = new Array(n + 1);
	for (let j = 0; j <= n; j++) prev[j] = j;
	for (let i = 1; i <= m; i++) {
		cur[0] = i;
		let rowMin = cur[0];
		for (let j = 1; j <= n; j++) {
			const cost = a.charCodeAt(i - 1) === b.charCodeAt(j - 1) ? 0 : 1;
			cur[j] = Math.min(prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost);
			if (cur[j] < rowMin) rowMin = cur[j];
		}
		if (rowMin > max) return max + 1;
		const tmp = prev;
		prev = cur;
		cur = tmp;
	}
	return prev[n];
}

function shareKeywordHitIn(hay) {
	const keys = parseShareKeywords();
	if (!keys.length || !hay) return "";
	const folded = foldShareText(hay);
	if (!folded) return "";
	const raw = folded.split(/\s+/).filter(Boolean);
	const words = raw.filter((w) => w.length >= 2);
	if (!raw.length) return "";
	for (const k of keys) {
		const parts = k.split(/\s+/).filter(Boolean);
		const compact = parts.join("");
		if (!compact) continue;
		if (words.some((w) => shareWordFitsKey(w, compact))) return k;
		if (parts.length > 1) {
			for (let i = 0; i + parts.length <= words.length; i++) {
				let ok = true;
				for (let j = 0; j < parts.length; j++) {
					if (!shareWordFitsKey(words[i + j], parts[j])) {
						ok = false;
						break;
					}
				}
				if (ok) return k;
			}
		}
		if (compact.length >= 4) {
			for (let i = 0; i + 1 < raw.length; i++) {
				if (raw[i] + raw[i + 1] === compact) return k;
			}
		}
	}
	return "";
}

function shareKeywordPauseAllowed() {
	return cloud.shareKeywordPause === true;
}

function syncShareKeywordUi() {
	$("share-keyword-block")?.classList.toggle("hidden", !shareKeywordPauseAllowed());
	const on = shareKeywordPauseAllowed() && !!$("share-opt-keyword-pause")?.checked;
	$("share-keyword-delay-warn")?.classList.toggle("hidden", !on);
	if (!shareKeywordPauseAllowed()) stopShareKeywordWatch();
}

function persistShareKeywordSettings() {
	if (!shareKeywordPauseAllowed()) {
		stopShareKeywordWatch();
		syncShareKeywordUi();
		return;
	}
	if ($("share-opt-keyword-pause"))
		settings.shareKeywordPause = $("share-opt-keyword-pause").checked;
	if ($("share-keywords"))
		settings.shareKeywords = $("share-keywords").value;
	try { localStorage.setItem(SETTINGS_KEY, JSON.stringify(settings)); } catch {}
	scheduleCloudPush();
	if (share.active && !share.isGuest) startShareKeywordWatch();
	else stopShareKeywordWatch();
	syncShareAudioDelay();
	updateShareKeywordStatus();
	syncShareKeywordUi();
}

function setShareKeywordStatus(text) {
	const el = $("share-keyword-status");
	if (!el) return;
	el.textContent = text || "";
}

function updateShareKeywordStatus() {
	if (!shareKeywordPauseAllowed() || !settings.shareKeywordPause) {
		setShareKeywordStatus("");
		return;
	}
	if (!share.active || !streaming) {
		setShareKeywordStatus(t("share.keywordIdle"));
		return;
	}
	if (share.ocrErr) {
		setShareKeywordStatus(t("share.keywordErr"));
		return;
	}
	if (share.paused && share.lastHitKey) {
		setShareKeywordStatus(t("share.keywordHit", { k: share.lastHitKey }));
		return;
	}
	setShareKeywordStatus("");
}

const SHARE_OCR_RESUME_MISSES = 1;
const SHARE_OCR_HIT_STREAK = 1;
const SHARE_OCR_RESUME_MS = 1600;
const SHARE_KEYWORD_DELAY_MS = 2500;

function shareDelayFps() {
	return 20;
}

function shareKeywordDelayOn() {
	return !share.isGuest && share.active && shareKeywordPauseAllowed() && !!settings.shareKeywordPause;
}

function keepAliveShareCap() {
	const track = share.media?.getVideoTracks()[0];
	if (track && typeof track.requestFrame === "function")
		try { track.requestFrame(); } catch {}
}

function clearShareDelayQueue() {
	share.delayAt = 0;
	share.delayShownT = 0;
	if (!share.delayRing) return;
	for (const slot of share.delayRing) slot.t = 0;
}

function freeShareDelayRing() {
	clearShareDelayQueue();
	share.delayRing = null;
	share.delayI = 0;
	share.stageCanvas = null;
	share.stageCtx = null;
}

function delaySlotSize() {
	const cw = Math.max(2, share.capCanvas?.width || 1280);
	const ch = Math.max(2, share.capCanvas?.height || 720);
	let w = cw;
	let h = ch;
	if (w > 1280) {
		h = Math.max(2, Math.round(ch * (1280 / w)));
		w = 1280;
	}
	if (w & 1) w++;
	if (h & 1) h++;
	return [w, h];
}

function ensureShareStage() {
	const c = share.capCanvas;
	if (!c) return false;
	if (share.stageCanvas && share.stageCanvas.width === c.width && share.stageCanvas.height === c.height)
		return !!share.stageCtx;
	const stage = document.createElement("canvas");
	stage.width = c.width;
	stage.height = c.height;
	share.stageCanvas = stage;
	share.stageCtx = stage.getContext("2d", { alpha: false });
	if (share.stageCtx) {
		share.stageCtx.imageSmoothingEnabled = false;
		share.stageCtx.fillStyle = "#000";
		share.stageCtx.fillRect(0, 0, stage.width, stage.height);
	}
	return !!share.stageCtx;
}

function ensureShareDelayRing() {
	if (!share.capCanvas) return false;
	const [w, h] = delaySlotSize();
	const n = Math.ceil(shareDelayFps() * (SHARE_KEYWORD_DELAY_MS / 1000) + 4);
	if (share.delayRing?.length === n && share.delayRing[0]?.canvas.width === w && share.delayRing[0]?.canvas.height === h)
		return true;
	const ring = [];
	for (let i = 0; i < n; i++) {
		const canvas = document.createElement("canvas");
		canvas.width = w;
		canvas.height = h;
		const ctx = canvas.getContext("2d", { alpha: false });
		if (ctx) {
			ctx.imageSmoothingEnabled = false;
			ctx.fillStyle = "#000";
			ctx.fillRect(0, 0, w, h);
		}
		ring.push({ canvas, ctx, t: 0 });
	}
	share.delayRing = ring;
	share.delayI = 0;
	share.delayAt = 0;
	share.delayShownT = 0;
	return true;
}

function enqueueShareDelayFrame() {
	if (!share.stageCanvas || !ensureShareDelayRing()) return;
	const now = performance.now();
	if (now - (share.delayAt || 0) < (1000 / shareDelayFps()) - 2) return;
	const ring = share.delayRing;
	const slot = ring[share.delayI % ring.length];
	try {
		slot.ctx.imageSmoothingEnabled = false;
		slot.ctx.drawImage(share.stageCanvas, 0, 0, slot.canvas.width, slot.canvas.height);
		slot.t = now;
		share.delayAt = now;
		share.delayI++;
	} catch {}
}

function flushShareDelayQueue() {
	if (!share.capCtx || !share.capCanvas) return;
	if (share.paused || !share.delayRing) {
		keepAliveShareCap();
		return;
	}
	const now = performance.now();
	const maxT = now - SHARE_KEYWORD_DELAY_MS;
	if (!(maxT > 0)) {
		keepAliveShareCap();
		return;
	}
	let best = null;
	for (const slot of share.delayRing) {
		if (!slot.t || slot.t > maxT || slot.t <= (share.delayShownT || 0)) continue;
		if (!best || slot.t > best.t) best = slot;
	}
	if (!best) {
		keepAliveShareCap();
		return;
	}
	try {
		share.capCtx.imageSmoothingEnabled = false;
		share.capCtx.drawImage(best.canvas, 0, 0, share.capCanvas.width, share.capCanvas.height);
		share.lastShareDraw = now;
		share.delayShownT = best.t;
		keepAliveShareCap();
	} catch {}
}

function syncShareAudioDelay() {
	if (!audio.ctx || !audio.gain || !share.audioDest) return;
	try { audio.gain.disconnect(share.audioDest); } catch {}
	if (share.audioDelay) {
		try { audio.gain.disconnect(share.audioDelay); } catch {}
		try { share.audioDelay.disconnect(); } catch {}
	}
	if (shareKeywordDelayOn()) {
		if (!share.audioDelay || share.audioDelay.delayTime.maxValue < SHARE_KEYWORD_DELAY_MS / 1000)
			share.audioDelay = audio.ctx.createDelay(4);
		share.audioDelay.delayTime.value = SHARE_KEYWORD_DELAY_MS / 1000;
		try { audio.gain.connect(share.audioDelay); } catch {}
		try { share.audioDelay.connect(share.audioDest); } catch {}
	} else {
		try { audio.gain.connect(share.audioDest); } catch {}
	}
}

function clearOcrBusyWatch() {
	if (share.ocrBusyTimer) {
		clearTimeout(share.ocrBusyTimer);
		share.ocrBusyTimer = 0;
	}
}

function armOcrBusyWatch() {
	clearOcrBusyWatch();
	share.ocrBusyTimer = setTimeout(() => {
		share.ocrBusyTimer = 0;
		share.ocrBusy = false;
	}, 4000);
}

function finishShareOcrBusy() {
	share.ocrBusy = false;
	clearOcrBusyWatch();
}

function ingestShareOcrText(text, err) {
	if (!share.keywordWatch) return;
	if (!share.active || share.isGuest || !shareKeywordPauseAllowed() || !settings.shareKeywordPause || !streaming) {
		share.lastHitAt = 0;
		share.lastHitKey = "";
		share.ocrMissStreak = 0;
		share.ocrHitStreak = 0;
		setSharePaused(false);
		updateShareKeywordStatus();
		return;
	}
	if (err) {
		share.ocrErr = true;
		share.ocrHitStreak = 0;
		share.ocrMissStreak = (share.ocrMissStreak || 0) + 1;
		if (share.ocrMissStreak >= SHARE_OCR_RESUME_MISSES) {
			const snapAt = share.ocrPendingSnapAt || 0;
			if (snapAt > (share.ocrClearUntil || 0)) share.ocrClearUntil = snapAt;
			if (share.paused) setSharePaused(false);
		} else maybeResumeSharePause();
		updateShareKeywordStatus();
		return;
	}
	share.ocrErr = false;
	share.ocrHay = String(text || "");
	const hit = shareKeywordHitIn(share.ocrHay);
	if (hit) {
		share.lastHitAt = performance.now();
		share.lastHitKey = hit;
		share.ocrMissStreak = 0;
		share.ocrHitStreak = (share.ocrHitStreak || 0) + 1;
		if (share.ocrHitStreak >= SHARE_OCR_HIT_STREAK)
			setSharePaused(true);
	} else {
		share.ocrHitStreak = 0;
		const snapAt = share.ocrPendingSnapAt || 0;
		if (snapAt > (share.ocrClearUntil || 0)) share.ocrClearUntil = snapAt;
		share.ocrMissStreak = (share.ocrMissStreak || 0) + 1;
		if (share.paused && share.ocrMissStreak >= SHARE_OCR_RESUME_MISSES) {
			share.lastHitKey = "";
			setSharePaused(false);
		} else maybeResumeSharePause();
	}
	updateShareKeywordStatus();
}

function setSharePaused(on) {
	on = !!on;
	if (share.paused === on) {
		if (on) drawSharePausedCanvas();
		return;
	}
	share.paused = on;
	clearShareDelayQueue();
	applySharePauseMedia();
	if (on) drawSharePausedCanvas();
	else {
		share.lastHitKey = "";
		share.ocrClearUntil = performance.now();
	}
	shareReplaceOutgoingVideo();
	if (!on) shareForceKeyframes();
	shareBroadcastState();
	updateShareBanners();
}

function maybeResumeSharePause() {
	if (!share.paused || !share.keywordWatch) return;
	if (performance.now() - (share.lastHitAt || 0) < SHARE_OCR_RESUME_MS) return;
	share.lastHitKey = "";
	share.ocrHitStreak = 0;
	setSharePaused(false);
}

function applySharePauseMedia() {
	if (!share.media) return;
	for (const t of share.media.getAudioTracks()) {
		try { t.enabled = !share.paused && share.audio !== false; } catch {}
	}
}

function drawSharePausedCanvas() {
	const c = share.capCanvas;
	const ctx = share.capCtx;
	if (!c || !ctx) return;
	const w = c.width || 1280;
	const h = c.height || 720;
	ctx.fillStyle = "#000";
	ctx.fillRect(0, 0, w, h);
	share.lastShareDraw = performance.now();
	const track = share.media?.getVideoTracks()[0];
	if (track && typeof track.requestFrame === "function")
		try { track.requestFrame(); } catch {}
}

function loadShareOcrScript() {
	if (window.Tesseract) return Promise.resolve();
	if (share.ocrScriptP) return share.ocrScriptP;
	share.ocrScriptP = new Promise((resolve, reject) => {
		const s = document.createElement("script");
		s.src = "https://cdn.jsdelivr.net/npm/tesseract.js@5.1.1/dist/tesseract.min.js";
		s.async = true;
		s.onload = () => resolve();
		s.onerror = () => reject(new Error("ocr"));
		document.head.appendChild(s);
	});
	return share.ocrScriptP;
}

async function ensureShareOcrTess() {
	if (share.ocrTess) return share.ocrTess;
	if (share.ocrTessP) return share.ocrTessP;
	share.ocrTessP = (async () => {
		await loadShareOcrScript();
		if (!window.Tesseract) throw new Error("ocr");
		const worker = await window.Tesseract.createWorker("fra+eng", 1, {
			logger: () => {},
			workerPath: "https://cdn.jsdelivr.net/npm/tesseract.js@5.1.1/dist/worker.min.js",
			corePath: "https://cdn.jsdelivr.net/npm/tesseract.js-core@5.1.0/tesseract-core-simd.wasm.js",
			langPath: "https://tessdata.projectnaptha.com/4.0.0"
		});
		try {
			await worker.setParameters({
				tessedit_pageseg_mode: "6",
				preserve_interword_spaces: "1"
			});
		} catch {}
		share.ocrTess = worker;
		return worker;
	})().catch((err) => {
		share.ocrTessP = null;
		throw err;
	});
	return share.ocrTessP;
}

async function ocrShareBlob(blob) {
	if (!blob) return "";
	if (typeof TextDetector === "function") {
		try {
			if (!share.textDetector) share.textDetector = new TextDetector();
			const bmp = await createImageBitmap(blob);
			const boxes = await share.textDetector.detect(bmp);
			try { bmp.close(); } catch {}
			const text = (boxes || []).map((b) => b.rawValue || "").join(" ").trim();
			if (text && shareKeywordHitIn(text)) return text;
		} catch {}
	}
	const tess = await ensureShareOcrTess();
	const parts = [];
	try {
		const out = await tess.recognize(blob);
		const t6 = (out && out.data && out.data.text) || "";
		if (String(t6).trim()) parts.push(t6);
	} catch {}
	try {
		await tess.setParameters({ tessedit_pageseg_mode: "11" });
		const out = await tess.recognize(blob);
		const t11 = (out && out.data && out.data.text) || "";
		if (String(t11).trim()) parts.push(t11);
		await tess.setParameters({ tessedit_pageseg_mode: "6" });
	} catch {
		try { await tess.setParameters({ tessedit_pageseg_mode: "6" }); } catch {}
	}
	return parts.join(" ");
}

function ensureShareOcrPort() {
	if (share.ocrPort) return share.ocrPort;
	let port;
	try {
		port = new Worker("share-ocr-worker.js");
	} catch {
		share.ocrErr = true;
		return null;
	}
	port.onmessage = async (ev) => {
		const data = ev.data || {};
		finishShareOcrBusy();
		try {
			if (data.err) ingestShareOcrText("", true);
			else if (typeof data.text === "string") ingestShareOcrText(data.text, false);
			else if (data.blob) ingestShareOcrText(await ocrShareBlob(data.blob), false);
			else ingestShareOcrText("", true);
		} catch {
			ingestShareOcrText("", true);
		}
		if (share.keywordWatch && !share.ocrBusy) {
			maybeDrawOcrSnap(null, true);
			maybeOfferShareOcrFromCap();
		}
	};
	port.onerror = () => {
		finishShareOcrBusy();
		share.ocrErr = true;
	};
	share.ocrPort = port;
	return port;
}

function ensureOcrSnapCanvas(w, h) {
	w = Math.max(32, w | 0);
	h = Math.max(32, h | 0);
	if (!share.ocrSnap) {
		share.ocrSnap = document.createElement("canvas");
		share.ocrSnapCtx = share.ocrSnap.getContext("2d", { alpha: false });
	}
	if (share.ocrSnap.width !== w || share.ocrSnap.height !== h) {
		share.ocrSnap.width = w;
		share.ocrSnap.height = h;
	}
	return share.ocrSnapCtx;
}

function drawOcrSnapFrom(src, sx, sy, sw, sh) {
	if (!src || !sw || !sh) return;
	const w = Math.min(960, sw);
	const scale = w / sw;
	const fullH = Math.max(32, Math.round(sh * scale));
	const bandSrc = Math.max(16, Math.round(sh * 0.26));
	const bandH = Math.max(40, Math.round(bandSrc * scale * 1.65));
	const totalH = bandH + fullH + bandH;
	const ctx = ensureOcrSnapCanvas(w, totalH);
	if (!ctx) return;
	try {
		ctx.imageSmoothingEnabled = true;
		ctx.imageSmoothingQuality = "high";
		ctx.fillStyle = "#000";
		ctx.fillRect(0, 0, w, totalH);
		ctx.drawImage(src, sx, sy, sw, bandSrc, 0, 0, w, bandH);
		ctx.drawImage(src, sx, sy, sw, sh, 0, bandH, w, fullH);
		ctx.drawImage(src, sx, sy + sh - bandSrc, sw, bandSrc, 0, bandH + fullH, w, bandH);
		share.ocrSnapReady = true;
	} catch {}
}

function drawOcrSnap(frame) {
	if (!frame) return;
	const d = videoFrameDest(frame);
	if (!d.sw || !d.sh) return;
	drawOcrSnapFrom(frame, d.sx, d.sy, d.sw, d.sh);
}

function maybeDrawOcrSnap(frame, force) {
	if (!share.keywordWatch) return;
	if (share.ocrBusy && !force) return;
	const now = performance.now();
	if (!force && now - (share.ocrSnapAt || 0) < 280) return;
	try {
		if (share.stageCanvas?.width)
			drawOcrSnapFrom(share.stageCanvas, 0, 0, share.stageCanvas.width, share.stageCanvas.height);
		else if (!share.paused && share.capCanvas?.width)
			drawOcrSnapFrom(share.capCanvas, 0, 0, share.capCanvas.width, share.capCanvas.height);
		else return;
		share.ocrSnapAt = now;
	} catch {}
}

function postShareOcrBlob(src) {
	if (!src) {
		finishShareOcrBusy();
		return;
	}
	const send = (payload, transfer) => {
		try {
			const port = ensureShareOcrPort();
			if (!port) {
				finishShareOcrBusy();
				return;
			}
			payload.keys = parseShareKeywords();
			if (transfer) port.postMessage(payload, transfer);
			else port.postMessage(payload);
		} catch {
			finishShareOcrBusy();
		}
	};
	if (typeof src.toBlob !== "function") {
		finishShareOcrBusy();
		return;
	}
	try {
		src.toBlob((blob) => {
			if (!blob) {
				finishShareOcrBusy();
				return;
			}
			send({ blob });
		}, "image/jpeg", 0.82);
	} catch {
		finishShareOcrBusy();
	}
}

function maybeOfferShareOcrFromCap() {
	if (!shareKeywordPauseAllowed() || !share.keywordWatch || share.ocrBusy) return;
	if (!share.pcs.size && !(Number(share.viewers) > 0)) return;
	if (!share.ocrSnapReady || !share.ocrSnap || !share.ocrSnap.width) {
		maybeDrawOcrSnap(null, true);
		if (!share.ocrSnapReady) return;
	}
	if (share.ocrSnapAt && share.ocrSnapAt === share.ocrPendingSnapAt) return;
	const src = share.ocrSnap;
	share.ocrBusy = true;
	armOcrBusyWatch();
	share.ocrPendingSnapAt = share.ocrSnapAt || performance.now();
	if (typeof TextDetector === "function") {
		postShareOcrBlob(src);
		return;
	}
	postShareOcrBlob(src);
}

function startShareKeywordWatch() {
	if (share.isGuest || !share.active || !streaming || !shareKeywordPauseAllowed() || !settings.shareKeywordPause) {
		stopShareKeywordWatch();
		updateShareKeywordStatus();
		return;
	}
	if (share.keywordWatch) {
		ensureShareStage();
		ensureShareDelayRing();
		syncShareAudioDelay();
		updateShareKeywordStatus();
		return;
	}
	share.keywordWatch = true;
	share.ocrErr = false;
	finishShareOcrBusy();
	share.ocrMisses = 0;
	share.ocrMissStreak = 0;
	share.ocrHitStreak = 0;
	share.lastHitKey = "";
	share.ocrPendingSnapAt = 0;
	share.ocrClearUntil = 0;
	ensureShareStage();
	ensureShareDelayRing();
	syncShareAudioDelay();
	updateShareKeywordStatus();
	if (!share.ocrTimer)
		share.ocrTimer = setInterval(maybeOfferShareOcrFromCap, 280);
	maybeOfferShareOcrFromCap();
}

function stopShareKeywordWatch() {
	share.keywordWatch = false;
	share.ocrBusy = false;
	share.ocrHay = "";
	share.ocrSnapReady = false;
	share.ocrMisses = 0;
	share.lastHitAt = 0;
	share.lastHitKey = "";
	share.ocrClearUntil = 0;
	share.ocrMissStreak = 0;
	share.ocrHitStreak = 0;
	finishShareOcrBusy();
	if (share.ocrTimer) {
		clearInterval(share.ocrTimer);
		share.ocrTimer = 0;
	}
	if (share.paused) setSharePaused(false);
	syncShareAudioDelay();
	updateShareKeywordStatus();
}

function syncShareVpadGroupChecks() {
	document.querySelectorAll(".share-vpad-key-group").forEach((group) => {
		const boxes = [...group.querySelectorAll("input[data-vpad-key]")];
		const master = group.querySelector("input[data-vpad-group]");
		if (!master || !boxes.length) return;
		const n = boxes.filter((el) => el.checked).length;
		master.checked = n === boxes.length;
		master.indeterminate = n > 0 && n < boxes.length;
	});
}

function setShareVpadKeysChecked(on) {
	document.querySelectorAll("#share-vpad-keys-list input[data-vpad-key]").forEach((el) => {
		el.checked = !!on;
	});
	syncShareVpadGroupChecks();
}

function fillShareVpadKeyPicker() {
	const root = $("share-vpad-keys-list");
	if (!root || root.dataset.ready) return;
	root.innerHTML = VPAD_SHARE_GROUPS.map((group) => {
		const keys = group.keys.map((id) => VPAD_ITEMS.find((item) => item.id === id)).filter(Boolean);
		const chips = keys.map((item) => (
			`<label class="share-vpad-key">` +
			`<input type="checkbox" data-vpad-key="${item.id}" checked>` +
			`<span>${item.label}</span></label>`
		)).join("");
		return `<div class="share-vpad-key-group" data-group="${group.id}">` +
			`<h4><label class="check-label"><input type="checkbox" data-vpad-group="${group.id}" checked> ` +
			`<span data-i18n="keys.group.${group.id}">${group.id}</span></label></h4>` +
			`<div class="share-vpad-key-chips">${chips}</div></div>`;
	}).join("");
	root.dataset.ready = "1";
	root.addEventListener("change", (ev) => {
		if (ev.target.matches("input[data-vpad-group]")) {
			const on = ev.target.checked;
			ev.target.closest(".share-vpad-key-group")
				?.querySelectorAll("input[data-vpad-key]")
				.forEach((el) => { el.checked = on; });
		}
		syncShareVpadGroupChecks();
		if (share.active) saveShare();
	});
}

function filterPadByVpadKeys(pad, keys) {
	if (!Array.isArray(keys)) return pad;
	const allow = new Set(keys);
	let buttons = 0, l2 = 0, r2 = 0, lx = 0, ly = 0, rx = 0, ry = 0;
	for (const item of VPAD_ITEMS) {
		if (!allow.has(item.id)) continue;
		if (item.kind === "button" && item.bit && (pad.buttons & BTN[item.bit]))
			buttons |= BTN[item.bit];
		else if (item.kind === "trigger" && item.axis === "l2") l2 = Number(pad.l2) || 0;
		else if (item.kind === "trigger" && item.axis === "r2") r2 = Number(pad.r2) || 0;
		else if (item.kind === "stick" && item.id === "ls") {
			lx = Number(pad.lx) || 0;
			ly = Number(pad.ly) || 0;
		} else if (item.kind === "stick" && item.id === "rs") {
			rx = Number(pad.rx) || 0;
			ry = Number(pad.ry) || 0;
		}
	}
	return { ...pad, buttons, l2, r2, lx, ly, rx, ry };
}

function updateShareBanners() {
	const n = Number(share.viewers) || 0;
	const hostOn = !share.isGuest && share.active && streaming && (!share.bannerDismissed || share.paused);
	$("share-banner")?.classList.toggle("hidden", !hostOn);
	$("share-banner")?.classList.toggle("is-paused", !!share.paused);
	if ($("share-banner-text")) {
		$("share-banner-text").textContent = share.paused
			? t("share.paused")
			: t("share.banner", { n });
	}
	if ($("share-viewers")) $("share-viewers").textContent = String(n);
	if ($("share-link")) $("share-link").value = shareLinkUrl();
	if ($("share-toggle")) $("share-toggle").textContent = share.active ? t("share.disable") : t("share.enable");
	const state = $("share-stream-state");
	if (state) {
		state.textContent = streaming ? t("share.streamOn") : t("share.offline");
		state.classList.toggle("is-offline", !streaming);
	}
	document.body.classList.toggle("share-guest", share.isGuest);
}

function shareVpadSkinPayload() {
	const custom = {};
	const src = settings.vpadCustom && typeof settings.vpadCustom === "object" ? settings.vpadCustom : {};
	for (const [id, url] of Object.entries(src)) {
		if (typeof url === "string" && url.startsWith("data:image/")) custom[id] = url;
	}
	const layout = settings.vpadLayout && typeof settings.vpadLayout === "object" ? settings.vpadLayout : {};
	return {
		vpadTheme: vpadThemeId(),
		vpadCustom: custom,
		vpadLayout: layout,
		vpadOpacity: vpadOpacityPct(),
		vpadKeys: Array.isArray(share.vpadKeys) ? share.vpadKeys : shareVpadKeysFromForm()
	};
}

function shareBroadcastState(to) {
	if (!share.active || share.isGuest) return;
	const msg = {
		type: "status",
		streaming: !!streaming,
		vpad: !!share.vpad,
		video: share.video !== false,
		audio: share.audio !== false,
		gamepad: !!share.gamepad,
		paused: !!share.paused,
		language: settings.language === "fr" ? "fr" : "en",
		...shareVpadSkinPayload()
	};
	if (to) msg.to = to;
	shareSend(msg);
}

let shareSkinTimer = 0;
function shareBroadcastVpadSkin(delayMs) {
	if (!share.active || share.isGuest) return;
	if (shareSkinTimer) clearTimeout(shareSkinTimer);
	if (delayMs) {
		shareSkinTimer = setTimeout(() => {
			shareSkinTimer = 0;
			shareBroadcastState();
		}, delayMs);
		return;
	}
	shareSkinTimer = 0;
	shareBroadcastState();
}

function refreshGuestOverlay() {
	if (!share.isGuest) return;
	const v = $("share-player");
	const cover = $("share-pause-cover");
	v?.closest(".stage")?.classList.toggle("share-paused", !!share.paused);
	if (share.paused) {
		setStreamOverlay(t("share.paused"), "paused");
		v?.classList.add("hidden", "share-paused");
		cover?.classList.remove("hidden");
		return;
	}
	cover?.classList.add("hidden");
	v?.classList.remove("share-paused");
	if (share.video !== false) v?.classList.remove("hidden");
	else v?.classList.add("hidden");
	if (!share.hostStreaming) setStreamOverlay(t("share.offline"));
	else setStreamOverlay("");
	if (share.video !== false && v?.srcObject) v.play().catch(() => {});
}

function applyGuestShareStatus(msg) {
	if (!msg) return;
	const wasStreaming = share.hostStreaming;
	const wasPaused = !!share.paused;
	if (msg.streaming === false) share.hostStreaming = false;
	if (msg.streaming === true) share.hostStreaming = true;
	if (msg.video != null) share.video = msg.video !== false;
	if (msg.audio != null) share.audio = msg.audio !== false;
	if (msg.gamepad != null) share.gamepad = !!msg.gamepad;
	if (msg.vpad != null) share.vpad = !!msg.vpad;
	if (typeof msg.vpadTheme === "string") share.vpadTheme = msg.vpadTheme;
	if (msg.vpadCustom && typeof msg.vpadCustom === "object") share.vpadCustom = msg.vpadCustom;
	if (msg.vpadLayout && typeof msg.vpadLayout === "object") share.vpadLayout = msg.vpadLayout;
	if (msg.vpadOpacity != null) share.vpadOpacity = Number(msg.vpadOpacity);
	if (msg.vpadKeys !== undefined) share.vpadKeys = normalizeVpadKeys(msg.vpadKeys);
	if (msg.paused != null) share.paused = !!msg.paused;
	if (msg.language) applyShareHostLanguage(msg.language);
	if (!share.vpad) vpad = { buttons: 0, l2: 0, r2: 0, lx: 0, ly: 0, rx: 0, ry: 0 };
	if (share.video === false && !share.paused) $("share-player")?.classList.add("hidden");
	syncVpadUi();
	refreshGuestOverlay();
	if (share.hostStreaming && !share.paused && (!wasStreaming || wasPaused)) {
		const v = $("share-player");
		if (v?.srcObject) {
			v.classList.remove("hidden", "share-paused");
			v.play().catch(() => {});
		}
		shareSend({ type: "pli" });
	}
}

function showShareError(key) {
	const el = $("share-error");
	if (!el) return;
	el.textContent = key ? t(key) : "";
	el.classList.toggle("hidden", !key);
}

let shareModalRestoreFs = false;

async function openShareModal() {
	if (isElectronApp()) return;
	closeAccountMenu();
	showShareError("");
	shareModalRestoreFs = isFullscreen() && currentView === "stream";
	if (shareModalRestoreFs) await exitStreamFullscreen();
	applyShareForm(share);
	updateShareBanners();
	$("share-modal")?.classList.remove("hidden");
}

async function closeShareModal() {
	$("share-modal")?.classList.add("hidden");
	if (shareModalRestoreFs && streaming && currentView === "stream") {
		shareModalRestoreFs = false;
		await enterStreamFullscreen();
	} else {
		shareModalRestoreFs = false;
	}
}

function bindShareBannerDrag(el) {
	if (!el) return;
	let drag = null;
	el.addEventListener("pointerdown", (e) => {
		if (e.target.closest("button")) return;
		const r = el.getBoundingClientRect();
		drag = { dx: e.clientX - r.left, dy: e.clientY - r.top };
		el.style.left = r.left + "px";
		el.style.top = r.top + "px";
		el.style.right = "auto";
		el.style.bottom = "auto";
		el.style.transform = "none";
		try { el.setPointerCapture(e.pointerId); } catch {}
	});
	el.addEventListener("pointermove", (e) => {
		if (!drag) return;
		const x = Math.max(8, Math.min(window.innerWidth - el.offsetWidth - 8, e.clientX - drag.dx));
		const y = Math.max(8, Math.min(window.innerHeight - el.offsetHeight - 8, e.clientY - drag.dy));
		el.style.left = x + "px";
		el.style.top = y + "px";
	});
	el.addEventListener("pointerup", () => { drag = null; });
	el.addEventListener("pointercancel", () => { drag = null; });
}

function applyShareData(data) {
	if (!data) return;
	share.token = data.token || share.token || "";
	const wasActive = share.active;
	share.active = !!data.active;
	if (share.active && !wasActive) share.bannerDismissed = false;
	share.video = data.video !== false;
	share.audio = data.audio !== false;
	share.vpad = data.vpad !== false;
	share.gamepad = !!data.gamepad;
	share.vpadKeys = normalizeVpadKeys(data.vpadKeys);
	if (data.viewers != null) share.viewers = Number(data.viewers) || 0;
	applyShareForm(share);
	updateShareBanners();
}

async function loadShareState() {
	if (share.isGuest || isElectronApp()) return;
	const { ok, body } = await cloudRequest("/api/share");
	if (!ok) return;
	applyShareData(body);
	if (share.active) shareConnectHost();
	else shareDisconnectHost();
}

async function saveShare(extra) {
	showShareError("");
	const body = { ...shareRightsFromForm(), active: share.active, ...(extra || {}) };
	const { ok, body: out } = await cloudRequest("/api/share", {
		method: "POST",
		body: JSON.stringify(body)
	});
	if (!ok) {
		showShareError("share.missing");
		return null;
	}
	applyShareData(out);
	if (share.active) {
		shareConnectHost();
		shareBroadcastState();
		startShareKeywordWatch();
	} else shareDisconnectHost();
	return out;
}

function icePayload(c) {
	if (!c) return null;
	if (typeof c.toJSON === "function") return c.toJSON();
	return {
		candidate: c.candidate,
		sdpMid: c.sdpMid,
		sdpMLineIndex: c.sdpMLineIndex,
		usernameFragment: c.usernameFragment
	};
}

function sdpPayload(desc) {
	if (!desc) return null;
	return { type: desc.type, sdp: desc.sdp };
}

function shareForceKeyframes() {
	for (const pc of share.pcs.values()) {
		for (const sender of pc.getSenders()) {
			if (sender.track && sender.track.kind === "video")
				try { sender.generateKeyFrame?.(); } catch {}
		}
	}
}

function ensureShareBlankTrack() {
	if (share.blankTrack && share.blankTrack.readyState !== "ended") return share.blankTrack;
	const c = document.createElement("canvas");
	c.width = 64;
	c.height = 36;
	const ctx = c.getContext("2d", { alpha: false });
	if (ctx) {
		ctx.fillStyle = "#000";
		ctx.fillRect(0, 0, c.width, c.height);
	}
	share.blankCanvas = c;
	const stream = c.captureStream(5);
	share.blankTrack = stream.getVideoTracks()[0] || null;
	if (share.blankTrack) {
		try { share.blankTrack.contentHint = "motion"; } catch {}
		try { share.blankTrack.enabled = true; } catch {}
		if (typeof share.blankTrack.requestFrame === "function")
			try { share.blankTrack.requestFrame(); } catch {}
	}
	return share.blankTrack;
}

function shareOutgoingVideoTrack() {
	if (share.paused) return ensureShareBlankTrack();
	return share.media?.getVideoTracks()[0] || null;
}

function shareReplaceOutgoingVideo() {
	const track = shareOutgoingVideoTrack();
	if (!track) return;
	for (const pc of share.pcs.values()) {
		for (const sender of pc.getSenders()) {
			if (!sender.track || sender.track.kind !== "video") continue;
			if (sender.track === track) continue;
			try { sender.replaceTrack(track); } catch {}
		}
	}
	shareForceKeyframes();
}

function shareCaptureFps() {
	const fps = Number(settings.fps) || 60;
	return fps <= 30 ? 30 : 60;
}

function shareEvenDim(n) {
	n = Math.max(2, Math.round(Number(n) || 0));
	return n + (n & 1);
}

function shareBitrateBps() {
	const kbps = effectiveBitrate();
	return Math.min(20_000_000, Math.max(6_000_000, kbps * 1000));
}

function preferShareVideoCodecs(pc) {
	if (!pc || typeof RTCRtpSender?.getCapabilities !== "function") return;
	const caps = RTCRtpSender.getCapabilities("video");
	if (!caps?.codecs?.length) return;
	const rank = (mime) => {
		const m = String(mime || "").toLowerCase();
		if (m === "video/h264") return 0;
		if (m === "video/vp9") return 1;
		if (m === "video/av1") return 2;
		if (m === "video/vp8") return 3;
		return 9;
	};
	const sorted = caps.codecs.slice().sort((a, b) => rank(a.mimeType) - rank(b.mimeType));
	for (const t of pc.getTransceivers())
		try { t.setCodecPreferences(sorted); } catch {}
}

function startShareKeyframeLoop() {
	if (share.keyTimer) return;
	share.keyTimer = setInterval(() => {
		if (!share.active || share.isGuest) return;
		shareForceKeyframes();
	}, 2500);
}

function startShareCaptureLoop() {
	if (share.keepAlive) return;
	share.keepAlive = true;
	const tick = () => {
		if (share.keepAlive) requestAnimationFrame(tick);
		if (share.paused) {
			maybeResumeSharePause();
			if (performance.now() - (share.lastShareDraw || 0) > 200)
				drawSharePausedCanvas();
			return;
		}
		if (shareKeywordDelayOn()) {
			flushShareDelayQueue();
			return;
		}
		if (!share.capCtx || !share.capCanvas) return;
		const track = share.media?.getVideoTracks()[0];
		if (track && typeof track.requestFrame === "function" && performance.now() - share.lastShareDraw < 250)
			try { track.requestFrame(); } catch {}
	};
	requestAnimationFrame(tick);
}

function pushShareFrame(frame) {
	if (!share.active || share.isGuest || !share.pcs.size || !share.capCtx || !share.capCanvas) return;
	if (!frame) return;
	const d = videoFrameDest(frame);
	if (!d.sw || !d.sh) return;
	if (shareKeywordDelayOn() || share.keywordWatch) {
		if (!ensureShareStage()) return;
		try {
			share.stageCtx.imageSmoothingEnabled = false;
			share.stageCtx.drawImage(frame, d.sx, d.sy, d.sw, d.sh, 0, 0, share.stageCanvas.width, share.stageCanvas.height);
		} catch { return; }
		if (share.paused) {
			maybeResumeSharePause();
			drawSharePausedCanvas();
			return;
		}
		if (shareKeywordDelayOn()) {
			enqueueShareDelayFrame();
			return;
		}
	}
	if (share.paused) {
		maybeResumeSharePause();
		drawSharePausedCanvas();
		return;
	}
	try {
		share.capCtx.imageSmoothingEnabled = false;
		share.capCtx.drawImage(frame, d.sx, d.sy, d.sw, d.sh, 0, 0, share.capCanvas.width, share.capCanvas.height);
		share.lastShareDraw = performance.now();
		const track = share.media?.getVideoTracks()[0];
		if (track && typeof track.requestFrame === "function") track.requestFrame();
	} catch {}
}

function ensureShareMedia() {
	if (!share.media && typeof HTMLCanvasElement !== "undefined" && HTMLCanvasElement.prototype.captureStream) {
		const c = document.createElement("canvas");
		const [pw, ph] = streamPresetSize();
		const w = shareEvenDim(pw || canvas.width || 1920);
		const h = shareEvenDim(ph || canvas.height || 1080);
		c.width = w;
		c.height = h;
		c.className = "share-cap";
		c.setAttribute("aria-hidden", "true");
		c.style.cssText = `position:fixed;left:-10000px;top:0;width:${w}px;height:${h}px;opacity:0;pointer-events:none;`;
		document.body.appendChild(c);
		share.capCanvas = c;
		share.capCtx = c.getContext("2d", { alpha: false });
		if (share.capCtx) {
			share.capCtx.imageSmoothingEnabled = false;
			share.capCtx.fillStyle = "#000";
			share.capCtx.fillRect(0, 0, w, h);
		}
		share.media = c.captureStream(shareCaptureFps());
		const vt = share.media.getVideoTracks()[0];
		if (vt) {
			try { vt.contentHint = "motion"; } catch {}
			try { vt.enabled = true; } catch {}
			if (typeof vt.requestFrame === "function")
				try { vt.requestFrame(); } catch {}
		}
	}
	startShareCaptureLoop();
	attachShareAudio();
	startShareKeyframeLoop();
	return share.media;
}

function stopShareMedia() {
	share.keepAlive = false;
	share.capRefresh = false;
	if (share.keyTimer) {
		clearInterval(share.keyTimer);
		share.keyTimer = null;
	}
	share.lastShareDraw = 0;
	freeShareDelayRing();
	if (share.audioDelay) {
		try { audio.gain?.disconnect(share.audioDelay); } catch {}
		try { share.audioDelay.disconnect(); } catch {}
		share.audioDelay = null;
	}
	if (share.media) {
		for (const t of share.media.getTracks()) {
			try { t.stop(); } catch {}
		}
		share.media = null;
	}
	if (share.capCanvas) {
		try { share.capCanvas.remove(); } catch {}
		share.capCanvas = null;
		share.capCtx = null;
	}
	share.audioDest = null;
	if (share.blankTrack) {
		try { share.blankTrack.stop(); } catch {}
		share.blankTrack = null;
		share.blankCanvas = null;
	}
	if (share.ocrPort) {
		try { share.ocrPort.terminate(); } catch {}
		share.ocrPort = null;
	}
	if (share.ocrTess && typeof share.ocrTess.terminate === "function") {
		try { share.ocrTess.terminate(); } catch {}
		share.ocrTess = null;
		share.ocrTessP = null;
	}
	finishShareOcrBusy();
}

function attachShareAudio() {
	if (!audio.ctx || share.audioDest) return;
	share.audioDest = audio.ctx.createMediaStreamDestination();
	const track = share.audioDest.stream.getAudioTracks()[0];
	if (share.media && track && !share.media.getAudioTracks().length)
		share.media.addTrack(track);
	syncShareAudioDelay();
	applySharePauseMedia();
}

async function configureShareSender(pc) {
	preferShareVideoCodecs(pc);
	for (const sender of pc.getSenders()) {
		if (sender.track?.kind !== "video") continue;
		try { sender.track.contentHint = "motion"; } catch {}
		try {
			const params = sender.getParameters();
			if (!params.encodings || !params.encodings.length) params.encodings = [{}];
			params.degradationPreference = "maintain-resolution";
			params.encodings[0].maxFramerate = shareCaptureFps();
			params.encodings[0].maxBitrate = shareBitrateBps();
			params.encodings[0].scaleResolutionDownBy = 1;
			params.encodings[0].priority = "high";
			params.encodings[0].networkPriority = "high";
			await sender.setParameters(params);
		} catch {}
		try { sender.generateKeyFrame?.(); } catch {}
	}
}

async function shareSyncPeerTracks() {
	const stream = ensureShareMedia();
	if (!stream) return;
	for (const [id, pc] of share.pcs) {
		for (const track of stream.getTracks()) {
			if (track.kind === "video") {
				if (!share.video) continue;
				const out = shareOutgoingVideoTrack();
				if (!out) continue;
				const sender = pc.getSenders().find((s) => s.track && s.track.kind === "video");
				try {
					if (sender) {
						if (sender.track !== out) await sender.replaceTrack(out);
					} else pc.addTrack(out, stream);
				} catch {}
				continue;
			}
			if (track.kind === "audio" && !share.audio) continue;
			const sender = pc.getSenders().find((s) => s.track && s.track.kind === track.kind);
			try {
				if (sender) {
					if (sender.track !== track) await sender.replaceTrack(track);
				} else pc.addTrack(track, stream);
			} catch {}
		}
		preferShareVideoCodecs(pc);
		try {
			const offer = await pc.createOffer();
			await pc.setLocalDescription(offer);
			shareSend({ type: "offer", to: id, sdp: sdpPayload(pc.localDescription) });
		} catch {}
	}
}

async function shareStartPeer(guestId) {
	const prev = share.pcs.get(guestId);
	if (prev) {
		try { prev.close(); } catch {}
		share.pcs.delete(guestId);
		share.iceWait.delete(guestId);
	}
	const pc = new RTCPeerConnection(SHARE_ICE);
	share.pcs.set(guestId, pc);
	share.iceWait.set(guestId, { queue: [], ready: false });
	const stream = ensureShareMedia();
	if (stream) {
		for (const track of stream.getTracks()) {
			if (track.kind === "video") {
				if (!share.video) continue;
				const out = shareOutgoingVideoTrack();
				if (out) pc.addTrack(out, stream);
				continue;
			}
			if (track.kind === "audio" && !share.audio) continue;
			pc.addTrack(track, stream);
		}
	}
	preferShareVideoCodecs(pc);
	pc.onicecandidate = (ev) => {
		if (ev.candidate) shareSend({ type: "ice", to: guestId, candidate: icePayload(ev.candidate) });
	};
	pc.onconnectionstatechange = () => {
		if (pc.connectionState === "connected") {
			configureShareSender(pc);
			shareForceKeyframes();
		}
	};
	try {
		const offer = await pc.createOffer();
		await pc.setLocalDescription(offer);
		await configureShareSender(pc);
		shareSend({ type: "offer", to: guestId, sdp: sdpPayload(pc.localDescription) });
	} catch {}
}

function shareDisconnectHost() {
	for (const pc of share.pcs.values()) {
		try { pc.close(); } catch {}
	}
	share.pcs.clear();
	share.iceWait.clear();
	share.guestPads.clear();
	share.pendingGuests.clear();
	share.viewers = 0;
	if (share.ws) {
		try { share.ws.close(); } catch {}
		share.ws = null;
	}
	if (!share.active) stopShareMedia();
	stopShareKeywordWatch();
	updateShareBanners();
}

function shareConnectHost() {
	if (share.isGuest || !share.active) return;
	if (share.ws && (share.ws.readyState === 0 || share.ws.readyState === 1)) return;
	const ws = new WebSocket(shareWsUrl());
	share.ws = ws;
	ws.onmessage = async (ev) => {
		let msg;
		try { msg = JSON.parse(ev.data); } catch { return; }
		if (msg.type === "hello") {
			share.viewers = msg.viewers || 0;
			if (msg.rights) applyShareData({ ...msg.rights, token: share.token, active: true });
			updateShareBanners();
		}
		if (msg.type === "guest-join") {
			share.viewers = msg.viewers || 0;
			updateShareBanners();
			if (streaming) await shareStartPeer(msg.id);
			else share.pendingGuests.add(msg.id);
			shareBroadcastState(msg.id);
		}
		if (msg.type === "guest-leave") {
			share.viewers = msg.viewers || 0;
			share.pendingGuests.delete(msg.id);
			const pc = share.pcs.get(msg.id);
			if (pc) { try { pc.close(); } catch {} share.pcs.delete(msg.id); }
			share.iceWait.delete(msg.id);
			share.guestPads.delete(msg.id);
			updateShareBanners();
		}
		if (msg.type === "answer" && msg.from) {
			const pc = share.pcs.get(msg.from);
			if (pc && msg.sdp) {
				try {
					await pc.setRemoteDescription(msg.sdp);
					const wait = share.iceWait.get(msg.from);
					if (wait) {
						wait.ready = true;
						while (wait.queue.length) {
							try { await pc.addIceCandidate(new RTCIceCandidate(wait.queue.shift())); } catch {}
						}
					}
				} catch {}
			}
		}
		if (msg.type === "ice" && msg.from && msg.candidate) {
			const pc = share.pcs.get(msg.from);
			const wait = share.iceWait.get(msg.from);
			if (wait && !wait.ready) wait.queue.push(msg.candidate);
			else if (pc) try { await pc.addIceCandidate(new RTCIceCandidate(msg.candidate)); } catch {}
		}
		if (msg.type === "pad" && msg.from)
			share.guestPads.set(msg.from, msg);
		if (msg.type === "pli") shareForceKeyframes();
	};
	ws.onclose = () => { if (share.ws === ws) share.ws = null; };
	ws.onerror = () => {};
}

function shareOnStreaming(on) {
	updateShareBanners();
	if (!share.active || share.isGuest) return;
	shareBroadcastState();
	if (on) {
		if (share.paused) setSharePaused(false);
		const waiting = [...share.pendingGuests];
		share.pendingGuests.clear();
		for (const id of waiting) shareStartPeer(id);
		if (share.pcs.size) shareForceKeyframes();
		startShareKeywordWatch();
	} else stopShareKeywordWatch();
}

function mergeGuestPad(buttons, l2, r2, lx, ly, rx, ry) {
	if (!share.active || share.isGuest || share.paused || (!share.vpad && !share.gamepad))
		return { buttons, l2, r2, lx, ly, rx, ry };
	for (const p of share.guestPads.values()) {
		const src = filterPadByVpadKeys(p, share.vpadKeys);
		buttons |= Number(src.buttons) || 0;
		l2 = Math.max(l2, Number(src.l2) || 0);
		r2 = Math.max(r2, Number(src.r2) || 0);
		if (src.lx) lx = Number(src.lx);
		if (src.ly) ly = Number(src.ly);
		if (src.rx) rx = Number(src.rx);
		if (src.ry) ry = Number(src.ry);
	}
	return { buttons, l2, r2, lx, ly, rx, ry };
}

function guestPadSnapshot() {
	let buttons = 0, l2 = 0, r2 = 0, lx = 0, ly = 0, rx = 0, ry = 0;
	if (share.gamepad) {
		const gp = pickGamepad();
		if (gp) {
			if (gp.buttons[0]?.pressed) buttons |= BTN.CROSS;
			if (gp.buttons[1]?.pressed) buttons |= BTN.MOON;
			if (gp.buttons[2]?.pressed) buttons |= BTN.BOX;
			if (gp.buttons[3]?.pressed) buttons |= BTN.PYRAMID;
			if (gp.buttons[4]?.pressed) buttons |= BTN.L1;
			if (gp.buttons[5]?.pressed) buttons |= BTN.R1;
			if (gp.buttons[8]?.pressed) buttons |= BTN.SHARE;
			if (gp.buttons[9]?.pressed) buttons |= BTN.OPTIONS;
			if (gp.buttons[10]?.pressed) buttons |= BTN.L3;
			if (gp.buttons[11]?.pressed) buttons |= BTN.R3;
			if (gp.buttons[12]?.pressed) buttons |= BTN.UP;
			if (gp.buttons[13]?.pressed) buttons |= BTN.DOWN;
			if (gp.buttons[14]?.pressed) buttons |= BTN.LEFT;
			if (gp.buttons[15]?.pressed) buttons |= BTN.RIGHT;
			if (gp.buttons[16]?.pressed) buttons |= BTN.PS;
			if (gp.buttons[17]?.pressed) buttons |= BTN.TOUCHPAD;
			l2 = Math.round((gp.buttons[6]?.value || 0) * 255);
			r2 = Math.round((gp.buttons[7]?.value || 0) * 255);
			const ax = padStickAxes(gp);
			lx = stick(deadzone(ax.lx));
			ly = stick(deadzone(ax.ly));
			rx = stick(deadzone(ax.rx));
			ry = stick(deadzone(ax.ry));
		}
	}
	if (share.vpad && vpadOn) {
		buttons |= vpad.buttons;
		l2 = Math.max(l2, vpad.l2);
		r2 = Math.max(r2, vpad.r2);
		if (vpadStickActive("ls")) { lx = vpad.lx; ly = vpad.ly; }
		if (vpadStickActive("rs")) { rx = vpad.rx; ry = vpad.ry; }
	}
	const pad = filterPadByVpadKeys({ buttons, l2, r2, lx, ly, rx, ry }, share.vpadKeys);
	return { type: "pad", ...pad };
}

async function bootGuest(token) {
	share.isGuest = true;
	share.token = token;
	document.body.classList.add("share-guest");
	const { ok, body } = await cloudRequest("/api/share/join?token=" + encodeURIComponent(token));
	if (!ok) {
		setBootScreen(false);
		alert(t("share.missing"));
		location.replace(location.pathname);
		return;
	}
	share.video = body.video !== false;
	share.audio = body.audio !== false;
	share.vpad = !!body.vpad;
	share.gamepad = !!body.gamepad;
	share.vpadKeys = normalizeVpadKeys(body.vpadKeys);
	share.viewers = Number(body.viewers) || 0;
	if (body.language) {
		share.hostLang = shareLangCode(body.language);
		await loadI18n(share.hostLang);
		applyI18n();
	}
	share.hostStreaming = false;
	$("share-player")?.classList.toggle("hidden", !share.video);
	await showView("stream");
	syncVpadUi();
	setStreamOverlay(t("share.offline"));
	setBootScreen(false);
	const pc = new RTCPeerConnection(SHARE_ICE);
	preferShareVideoCodecs(pc);
	const iceQueue = [];
	let remoteReady = false;
	const v = $("share-player");
	const playShareVideo = () => {
		if (!v || !v.srcObject || share.paused) return;
		const run = () => v.play().catch(() => {});
		run();
		if (v.paused) {
			v.muted = true;
			run();
		}
	};
	if (v) {
		v.playsInline = true;
		v.autoplay = true;
		try { v.disablePictureInPicture = true; } catch {}
		["stalled", "waiting", "suspend", "pause", "emptied"].forEach((ev) => {
			v.addEventListener(ev, () => {
				if (share.paused) return;
				if (v.srcObject && share.hostStreaming) playShareVideo();
			});
		});
	}
	pc.ontrack = (ev) => {
		if (!v) return;
		const stream = ev.streams[0] || new MediaStream([ev.track]);
		if (v.srcObject !== stream) v.srcObject = stream;
		if (!share.paused) v.classList.remove("hidden");
		v.playsInline = true;
		v.autoplay = true;
		playShareVideo();
		ev.track.onunmute = playShareVideo;
		ev.track.onmute = () => setTimeout(playShareVideo, 200);
		ev.track.onended = () => refreshGuestOverlay();
		refreshGuestOverlay();
	};
	pc.onicecandidate = (ev) => {
		if (ev.candidate) shareSend({ type: "ice", candidate: icePayload(ev.candidate) });
	};
	const ws = new WebSocket(shareWsUrl(token));
	share.ws = ws;
	ws.onmessage = async (ev) => {
		let msg;
		try { msg = JSON.parse(ev.data); } catch { return; }
		if (msg.type === "offer" && msg.sdp) {
			try {
				await pc.setRemoteDescription(msg.sdp);
				preferShareVideoCodecs(pc);
				remoteReady = true;
				while (iceQueue.length) {
					try { await pc.addIceCandidate(new RTCIceCandidate(iceQueue.shift())); } catch {}
				}
				const answer = await pc.createAnswer();
				await pc.setLocalDescription(answer);
				shareSend({ type: "answer", sdp: sdpPayload(pc.localDescription) });
			} catch {}
		}
		if (msg.type === "ice" && msg.candidate) {
			if (!remoteReady) iceQueue.push(msg.candidate);
			else try { await pc.addIceCandidate(new RTCIceCandidate(msg.candidate)); } catch {}
		}
		if (msg.type === "host-left") {
			share.hostStreaming = false;
			share.vpad = false;
			syncVpadUi();
			refreshGuestOverlay();
		}
		if (msg.type === "status" || msg.type === "rights")
			applyGuestShareStatus(msg);
		if (msg.type === "hello")
			applyGuestShareStatus({
				...(msg.rights || {}),
				language: msg.language || (msg.rights && msg.rights.language)
			});
	};
	setInterval(() => {
		if (!share.isGuest || share.paused || (!share.vpad && !share.gamepad)) return;
		shareSend(guestPadSnapshot());
	}, 16);
	let lastFrameCount = -1;
	let stuckHits = 0;
	let rvfcOn = false;
	let lastRvfc = 0;
	const watchFrames = () => {
		if (!v || typeof v.requestVideoFrameCallback !== "function") return;
		rvfcOn = true;
		const cb = () => {
			lastRvfc = performance.now();
			if (share.isGuest) v.requestVideoFrameCallback(cb);
		};
		v.requestVideoFrameCallback(cb);
	};
	watchFrames();
	setInterval(() => {
		if (!share.isGuest || share.paused || !share.hostStreaming || !v?.srcObject) {
			lastFrameCount = -1;
			stuckHits = 0;
			return;
		}
		playShareVideo();
		let progressing = false;
		if (rvfcOn)
			progressing = performance.now() - lastRvfc < 1500;
		else {
			const q = v.getVideoPlaybackQuality?.();
			const count = q ? q.totalVideoFrames : (v.webkitDecodedFrameCount || 0);
			progressing = count !== lastFrameCount && count > 0;
			lastFrameCount = count;
		}
		if (progressing) stuckHits = 0;
		else if (++stuckHits >= 5) {
			stuckHits = 0;
			shareSend({ type: "pli" });
		}
	}, 400);
}

async function waitForAuthIfNeeded() {
	if (!cloud.authEnabled || cloud.user) {
		refreshAuthUi();
		return;
	}
	refreshAuthUi();
	$("auth-screen")?.classList.remove("hidden");
	setBootScreen(false);
	await new Promise((resolve) => { authUnlocked = resolve; });
	refreshAuthUi();
}

async function pullCloudState() {
	if (!cloud.authEnabled) return true;
	const { ok, status, body } = await cloudRequest("/api/state");
	if (status === 401) return false;
	if (!ok) return true;
	applyRemoteState(body);
	return true;
}

function applyCloudMeta(body) {
	if (!body || typeof body !== "object") return;
	const wasHome = cloud.homeProxy;
	const wasPending = cloud.homeProxyPending;
	if (body.authEnabled != null) cloud.authEnabled = !!body.authEnabled;
	if (body.allowRegister != null) cloud.allowRegister = body.allowRegister !== false;
	if (body.maxHosts != null) cloud.maxHosts = Number(body.maxHosts) || 32;
	if (body.discoveryEnabled != null) cloud.discoveryEnabled = body.discoveryEnabled !== false;
	if (body.shareKeywordPause != null) cloud.shareKeywordPause = body.shareKeywordPause === true;
	if (body.user !== undefined) cloud.user = body.user || null;
	if (typeof body.ipv4 === "string") cloud.ipv4 = body.ipv4;
	if (body.homeProxy != null) cloud.homeProxy = !!body.homeProxy;
	if (body.homeProxyPending != null) cloud.homeProxyPending = !!body.homeProxyPending;
	if (typeof body.homeProxyName === "string") cloud.homeProxyName = body.homeProxyName;
	if (cloud.homeProxy) cloud.discoveryEnabled = true;
	if (homeProxyUiEnabled() && !wasHome && cloud.homeProxy && proxyState === "connected" && !proxyIsCustom()) {
		location.reload();
		return;
	}
	syncHomeProxyUi();
	applyDiscoveryUi();
	syncShareKeywordUi();
	if (!wasHome && cloud.homeProxy) startDiscovery();
	if (wasPending && !cloud.homeProxyPending && !cloud.homeProxy)
		ensureWasmRuntime().catch(() => {});
}

function renderProxyDownloads() {
	const box = $("proxy-dl-btns");
	const block = $("proxy-dl-block");
	if (!box) return;
	if (!homeProxyUiEnabled()) {
		block?.classList.add("hidden");
		return;
	}
	box.innerHTML = "";
	for (const it of [
		{ id: "windows", className: "proxy-dl-win", label: "config.proxyDlWin", fallback: "/downloads/chiaki-proxy-windows.exe" },
		{ id: "linux", className: "proxy-dl-win", label: "config.proxyDlLinux", fallback: "/downloads/chiaki-proxy-linux" },
		{ id: "macos", className: "ghost", label: "config.proxyDlMac", fallback: null }
	]) {
		const href = (proxyBuilds && proxyBuilds[it.id]) || it.fallback;
		if (!href) continue;
		const a = document.createElement("a");
		a.className = it.className;
		a.id = "proxy-dl-" + it.id;
		a.textContent = t(it.label);
		a.href = href;
		a.setAttribute("download", "");
		box.appendChild(a);
	}
	block?.classList.remove("hidden");
}

async function loadProxyDownloads() {
	try {
		const { ok, body } = await cloudRequest("/api/proxy-builds");
		if (ok && body && typeof body === "object") proxyBuilds = body;
	} catch {}
	renderProxyDownloads();
}

function syncHomeProxyUi() {
	if (!homeProxyUiEnabled()) {
		$("proxy-home-hint")?.classList.add("hidden");
		$("btn-home-proxy-disconnect")?.classList.add("hidden");
		$("home-proxy-modal")?.classList.add("hidden");
		$("add-home-hint")?.classList.add("hidden");
		refreshProxyStatus();
		return;
	}
	const hint = $("proxy-home-hint");
	if (hint) hint.classList.toggle("hidden", !cloud.homeProxy);
	$("btn-home-proxy-disconnect")?.classList.toggle("hidden", !cloud.homeProxy);
	const cfgHint = $("config-hint");
	if (cfgHint) cfgHint.setAttribute("data-i18n", cloud.homeProxy ? "config.homeProxyOn" : "config.hint");
	if (cfgHint && typeof t === "function") cfgHint.textContent = t(cloud.homeProxy ? "config.homeProxyOn" : "config.hint");
	const addHost = $("add-host");
	if (addHost) {
		const ph = cloud.homeProxy ? "add.addressPhHome" : "add.addressPh";
		addHost.setAttribute("data-i18n-placeholder", ph);
		addHost.placeholder = t(ph);
	}
	const addHint = $("add-home-hint");
	if (addHint) {
		if (cloud.homeProxy) {
			addHint.textContent = t("add.homeHint");
			addHint.classList.remove("hidden");
		} else if (cloud.homeProxyPending) {
			addHint.textContent = t("add.homeHintPending");
			addHint.classList.remove("hidden");
		} else {
			addHint.textContent = "";
			addHint.classList.add("hidden");
		}
	}
	refreshProxyStatus();
	syncHomeProxyModal();
}

function syncHomeProxyModal() {
	const modal = $("home-proxy-modal");
	if (!modal) return;
	const show = !!(homeProxyUiEnabled() && cloud.homeProxyPending && !cloud.homeProxy);
	const text = $("home-proxy-modal-text");
	if (text) text.textContent = t("config.proxyApproveText", { name: cloud.homeProxyName || "PC" });
	modal.classList.toggle("hidden", !show);
}

async function approveHomeProxy() {
	const { ok, body } = await cloudRequest("/api/proxy/approve", { method: "POST", body: "{}" });
	if (!ok) return;
	applyCloudMeta(body);
	location.reload();
}

async function disconnectHomeProxy() {
	const { ok, body } = await cloudRequest("/api/proxy/disconnect", { method: "POST", body: "{}" });
	if (ok) applyCloudMeta(body);
	$("home-proxy-modal")?.classList.add("hidden");
	location.reload();
}

async function rejectHomeProxy() {
	const { ok, body } = await cloudRequest("/api/proxy/reject", { method: "POST", body: "{}" });
	if (ok) applyCloudMeta(body);
	$("home-proxy-modal")?.classList.add("hidden");
	ensureWasmRuntime().catch(() => {});
}

async function initCloud() {
	try {
		const { ok, body } = await cloudRequest("/api/meta");
		if (ok) applyCloudMeta(body);
	} catch {}
	await waitForAuthIfNeeded();
	if (cloud.authEnabled) {
		try { await pullCloudState(); } catch {}
	}
	if (homeProxyUiEnabled()) {
		try {
			const { ok, body } = await cloudRequest("/api/meta");
			if (ok) applyCloudMeta(body);
		} catch {}
		if (!homeProxyWatch) {
			homeProxyWatch = setInterval(async () => {
				try {
					const { ok, body } = await cloudRequest("/api/meta");
					if (ok) applyCloudMeta(body);
				} catch {}
			}, 1500);
		}
		loadProxyDownloads();
	}
}

async function submitAuth(register) {
	showAuthError("");
	const email = ((register ? $("auth-reg-email") : $("auth-email"))?.value || "").trim();
	const password = (register ? $("auth-reg-pass") : $("auth-pass"))?.value || "";
	const username = ($("auth-reg-name")?.value || "").trim();
	const url = register ? "/api/register" : "/api/login";
	const { ok, body } = await cloudRequest(url, {
		method: "POST",
		body: JSON.stringify(register ? { username, email, password } : { email, password })
	});
	if (!ok) {
		const map = {
			invalid_credentials: "auth.bad",
			taken: "auth.taken",
			email_taken: "auth.emailTaken",
			weak_password: "auth.weak",
			invalid_username: "auth.invalid",
			invalid_email: "auth.invalidEmail",
			register_disabled: "auth.registerOff"
		};
		showAuthError(map[body.error] || "auth.bad");
		return;
	}
	applyCloudMeta(body);
	clearLocalAccountData();
	await pullCloudState();
	applyI18n();
	unlockAuth();
}
function defaultProxyUrl() {
	const proto = location.protocol === "https:" ? "wss" : "ws";
	return `${proto}://${location.host}/posix-net`;
}

function effectiveProxyUrl() {
	const custom = (settings.proxyUrl || "").trim();
	return custom || defaultProxyUrl();
}

function proxyIsCustom() {
	return !!(settings.proxyUrl || "").trim();
}

function syncProxyUi() {
	const custom = proxyIsCustom();
	if ($("s-proxy-mode")) $("s-proxy-mode").value = custom ? "custom" : "default";
	if ($("s-proxy-url")) $("s-proxy-url").value = custom ? settings.proxyUrl.trim() : "";
	$("proxy-custom-row")?.classList.toggle("hidden", !custom);
	refreshProxyStatus();
}

function refreshProxyStatus() {
	const el = $("proxy-status");
	if (!el) return;
	if (homeProxyUiEnabled() && cloud.homeProxy) el.textContent = t("config.homeProxyConnected");
	else if (homeProxyUiEnabled() && cloud.homeProxyPending) el.textContent = t("config.homeProxyPending");
	else if (proxyState === "failed") el.textContent = t("config.initFailed");
	else if (proxyState === "connected") el.textContent = t("config.connected");
	else if (proxyState === "offline") el.textContent = t("config.offline");
	else el.textContent = "—";
}

function commitProxyUrl(next) {
	const def = defaultProxyUrl();
	const stored = !next || next === def ? "" : next;
	if ((settings.proxyUrl || "") === stored) {
		syncProxyUi();
		return;
	}
	settings.proxyUrl = stored;
	localStorage.setItem(SETTINGS_KEY, JSON.stringify(settings));
	pushCloudState().finally(() => location.reload());
}

function applyProxyModeFromForm() {
	const mode = $("s-proxy-mode")?.value === "custom" ? "custom" : "default";
	if (mode === "default") {
		$("proxy-custom-row")?.classList.add("hidden");
		commitProxyUrl("");
		return;
	}
	$("proxy-custom-row")?.classList.remove("hidden");
	refreshProxyStatus();
	$("s-proxy-url")?.focus();
}

function applyCustomProxyFromForm() {
	const raw = ($("s-proxy-url")?.value || "").trim();
	if (!raw) {
		commitProxyUrl("");
		return;
	}
	if (/^(\d{1,3}\.){3}\d{1,3}$/.test(raw)) {
		log(t("config.proxyIpNotUrl"), 0);
		return;
	}
	if (!/^wss?:\/\//i.test(raw)) {
		log(t("config.proxyBad"), 0);
		return;
	}
	commitProxyUrl(raw);
}

function waitFor(pred, timeoutMs) {
	const start = Date.now();
	return new Promise((resolve, reject) => {
		const tick = () => {
			if (pred()) return resolve(true);
			if (Date.now() - start > timeoutMs) return reject(new Error("timeout"));
			setTimeout(tick, 50);
		};
		tick();
	});
}

function setBootScreen(on) {
	$("boot-screen")?.classList.toggle("hidden", !on);
}

async function loadConsoleIcons() {
	try {
		const [ps4, ps5] = await Promise.all([
			fetch("icons/console-ps4.svg").then((r) => r.text()),
			fetch("icons/console-ps5.svg").then((r) => r.text())
		]);
		svgIcons.ps4 = ps4.replace(/<\?xml[^>]*>/, "").replace(/<svg /, '<svg ');
		svgIcons.ps5 = ps5.replace(/<\?xml[^>]*>/, "");
	} catch (e) { log("Icons: " + e.message, 0); }
}

function parseAnnexB(u8) {
	const nals = [];
	const find = (from) => {
		for (let p = from; p + 3 < u8.length; p++) {
			if (u8[p] === 0 && u8[p + 1] === 0) {
				if (u8[p + 2] === 1) return { pos: p, size: 3 };
				if (p + 3 < u8.length && u8[p + 2] === 0 && u8[p + 3] === 1) return { pos: p, size: 4 };
			}
		}
		return null;
	};
	let cur = find(0);
	while (cur) {
		const next = find(cur.pos + cur.size);
		const start = cur.pos + cur.size;
		const end = next ? next.pos : u8.length;
		if (end > start) nals.push(u8.subarray(start, end));
		cur = next;
	}
	return nals;
}

function hevcNalType(nal) {
	return nal.length ? (nal[0] >> 1) & 0x3f : -1;
}

function avcNalType(nal) {
	return nal.length ? nal[0] & 0x1f : -1;
}

function isConfigNal(nal, hevc) {
	if (hevc) {
		const t = hevcNalType(nal);
		return t === 32 || t === 33 || t === 34;
	}
	const t = avcNalType(nal);
	return t === 7 || t === 8;
}

function isAudNal(nal, hevc) {
	return hevc ? hevcNalType(nal) === 35 : avcNalType(nal) === 9;
}

function isKeyNal(nal, hevc) {
	if (hevc) {
		const t = hevcNalType(nal);
		return t === 19 || t === 20 || t === 21;
	}
	return avcNalType(nal) === 5;
}

function nalRbsp(nal) {
	const out = [];
	for (let i = 0; i < nal.length; i++) {
		if (i + 2 < nal.length && nal[i] === 0 && nal[i + 1] === 0 && nal[i + 2] === 3) {
			out.push(0, 0);
			i += 2;
			continue;
		}
		out.push(nal[i]);
	}
	return new Uint8Array(out);
}

class BitReader {
	constructor(bytes) {
		this.d = bytes;
		this.i = 0;
	}
	bit() {
		const v = (this.d[this.i >> 3] >> (7 - (this.i & 7))) & 1;
		this.i++;
		return v;
	}
	bits(n) {
		let v = 0;
		for (let i = 0; i < n; i++) v = (v << 1) | this.bit();
		return v;
	}
	ue() {
		let z = 0;
		while (this.i < this.d.length * 8 && this.bit() === 0) z++;
		if (z === 0) return 0;
		return ((1 << z) | this.bits(z)) - 1;
	}
}

function parseAvcSpsSize(nal) {
	try {
		const rbsp = nalRbsp(nal);
		const br = new BitReader(rbsp);
		br.bits(8);
		const profile = br.bits(8);
		br.bits(8);
		br.bits(8);
		br.ue();
		if ([100, 110, 122, 244, 44, 83, 86, 118, 128, 138, 139, 134, 135].includes(profile)) {
			const chroma = br.ue();
			if (chroma === 3) br.bit();
			br.ue();
			br.ue();
			br.bit();
			if (br.bit()) {
				for (let i = 0; i < (chroma === 3 ? 12 : 8); i++) {
					if (br.bit()) {
						const len = i < 6 ? 16 : 64;
						let last = 8, next = 8;
						for (let j = 0; j < len; j++) {
							if (next) next = (last + (br.ue() & 1 ? -((br.ue() >> 1) + 1) : (br.ue() >> 1)) + 256) % 256;
							last = next || last;
						}
					}
				}
			}
		}
		br.ue();
		const poc = br.ue();
		if (poc === 0) br.ue();
		else if (poc === 1) {
			br.bit();
			br.ue();
			br.ue();
			const n = br.ue();
			for (let i = 0; i <= n; i++) br.ue();
		}
		br.ue();
		br.bit();
		const wMbs = br.ue();
		const hMap = br.ue();
		const frameMbs = br.bit();
		if (!frameMbs) br.bit();
		br.bit();
		let cropL = 0, cropR = 0, cropT = 0, cropB = 0;
		if (br.bit()) {
			cropL = br.ue();
			cropR = br.ue();
			cropT = br.ue();
			cropB = br.ue();
		}
		const width = (wMbs + 1) * 16 - (cropL + cropR) * 2;
		const height = (2 - frameMbs) * (hMap + 1) * 16 - (cropT + cropB) * 2;
		if (width >= 16 && height >= 16) return { width, height };
	} catch {}
	return null;
}

function parseHevcSpsSize(nal) {
	try {
		const rbsp = nalRbsp(nal);
		const br = new BitReader(rbsp);
		br.bits(16);
		br.bits(4);
		const maxSub = br.bits(3);
		br.bit();
		br.bits(2);
		br.bit();
		br.bits(5);
		br.bits(32);
		br.bits(48);
		br.bits(8);
		const subProfile = [];
		const subLevel = [];
		for (let i = 0; i < maxSub; i++) {
			subProfile[i] = br.bit();
			subLevel[i] = br.bit();
		}
		if (maxSub > 0) {
			for (let i = maxSub; i < 8; i++) br.bits(2);
		}
		for (let i = 0; i < maxSub; i++) {
			if (subProfile[i]) {
				br.bits(2);
				br.bit();
				br.bits(5);
				br.bits(32);
				br.bits(48);
			}
			if (subLevel[i]) br.bits(8);
		}
		br.ue();
		const chroma = br.ue();
		if (chroma === 3) br.bit();
		const width = br.ue();
		const height = br.ue();
		if (width >= 16 && height >= 16) return { width, height };
	} catch {}
	return null;
}

const STREAM_RES = {
	1: [640, 360],
	2: [960, 540],
	3: [1280, 720],
	4: [1920, 1080]
};

function streamPresetSize() {
	return STREAM_RES[Number(settings.resolution)] || [1920, 1080];
}

function videoFrameDest(frame) {
	const vr = frame.visibleRect;
	const dw = frame.displayWidth || 0;
	const dh = frame.displayHeight || 0;
	const cw = frame.codedWidth || 0;
	const ch = frame.codedHeight || 0;
	const vw = vr?.width || 0;
	const vh = vr?.height || 0;
	let sx = vr?.x || 0;
	let sy = vr?.y || 0;
	let sw = vw || dw || cw;
	let sh = vh || dh || ch;
	if (cw && sw && cw >= sw * 1.5 && ch >= sh * 1.5) {
		sx = 0;
		sy = 0;
		sw = cw;
		sh = ch;
	}
	return { sx, sy, sw, sh };
}

function packLengthPrefixed(nals) {
	let total = 0;
	for (const n of nals) total += 4 + n.length;
	const out = new Uint8Array(total);
	let o = 0;
	for (const n of nals) {
		out[o++] = (n.length >>> 24) & 255;
		out[o++] = (n.length >>> 16) & 255;
		out[o++] = (n.length >>> 8) & 255;
		out[o++] = n.length & 255;
		out.set(n, o);
		o += n.length;
	}
	return out;
}

function buildAvcC(sps, pps) {
	const out = new Uint8Array(11 + sps.length + pps.length);
	out[0] = 1;
	out[1] = sps[1] || 0x64;
	out[2] = sps[2] || 0;
	out[3] = sps[3] || 0x28;
	out[4] = 0xff;
	out[5] = 0xe1;
	out[6] = (sps.length >> 8) & 255;
	out[7] = sps.length & 255;
	out.set(sps, 8);
	let o = 8 + sps.length;
	out[o++] = 1;
	out[o++] = (pps.length >> 8) & 255;
	out[o++] = pps.length & 255;
	out.set(pps, o);
	return out;
}

function buildHvcC(vps, sps, pps) {
	const arrays = [
		{ type: 32, nal: vps },
		{ type: 33, nal: sps },
		{ type: 34, nal: pps }
	].filter((a) => a.nal && a.nal.length);
	let extra = 0;
	for (const a of arrays) extra += 5 + a.nal.length;
	const out = new Uint8Array(23 + extra);
	out[0] = 1;
	out[1] = 1;
	out[2] = 0x60;
	out[12] = 153;
	out[13] = 0xf0;
	out[15] = 0xfc;
	out[16] = 0xfd;
	out[17] = 0xf8;
	out[18] = 0xf8;
	out[21] = 0x03;
	out[22] = arrays.length;
	let o = 23;
	for (const a of arrays) {
		out[o++] = a.type;
		out[o++] = 0;
		out[o++] = 1;
		out[o++] = (a.nal.length >> 8) & 255;
		out[o++] = a.nal.length & 255;
		out.set(a.nal, o);
		o += a.nal.length;
	}
	return out.subarray(0, o);
}

function avcMimeFromSps(sps) {
	const p = (sps[1] || 0x64).toString(16).padStart(2, "0");
	const c = (sps[2] || 0).toString(16).padStart(2, "0");
	const l = (sps[3] || 0x28).toString(16).padStart(2, "0");
	return `avc1.${p}${c}${l}`.toLowerCase();
}

function collectConfig(nals, hevc) {
	const cfg = nals.filter((n) => isConfigNal(n, hevc));
	if (cfg.length) configPending = cfg.map((n) => n.slice());
}

function hasParamSets(hevc) {
	if (hevc) {
		const types = new Set(configPending.map(hevcNalType));
		return types.has(32) && types.has(33) && types.has(34);
	}
	const types = new Set(configPending.map(avcNalType));
	return types.has(7) && types.has(8);
}

async function codecConfigSupported(config) {
	if (typeof VideoDecoder === "undefined" || typeof VideoDecoder.isConfigSupported !== "function")
		return false;
	try {
		const r = await VideoDecoder.isConfigSupported(config);
		return !!(r && r.supported);
	} catch {
		return false;
	}
}

async function resolveStreamCodec(wanted) {
	if (typeof VideoDecoder !== "function") {
		log(t("log.webcodecsMissing"), 0);
		return 0;
	}
	if (wanted !== 1 && wanted !== 2) return 0;
	const hevcOk = await codecConfigSupported({ codec: "hev1.1.6.L153.B0", optimizeForLatency: true })
		|| await codecConfigSupported({ codec: "hvc1.1.6.L153.B0", optimizeForLatency: true })
		|| await codecConfigSupported({ codec: "hev1.1.6.L123.B0", optimizeForLatency: true });
	if (hevcOk) return 1;
	log(t("log.hevcFallback"));
	return 0;
}

function requestIdrThrottled() {
	const now = performance.now();
	if (now - lastIdrAt < 400) return;
	lastIdrAt = now;
	if (api.sessionRequestIdr) api.sessionRequestIdr();
}

function resetVideoDecoder() {
	decoderSetup = null;
	decoderReady = false;
	decoderFailed = false;
	gotKeyframe = false;
	configPending = [];
	videoTs = 0;
	lastIdrAt = 0;
	if (decoder) {
		try { decoder.close(); } catch {}
		decoder = null;
	}
}

function hwPreference() {
	if (settings.decoder === "prefer-software") return "prefer-software";
	if (settings.decoder === "prefer-hardware") return "prefer-hardware";
	return "no-preference";
}

async function configureDecoder() {
	const hevc = codec === 1;
	if (typeof VideoDecoder !== "function") throw new Error(t("log.webcodecsMissing"));
	const sps = configPending.find((n) => (hevc ? hevcNalType(n) === 33 : avcNalType(n) === 7));
	const pps = configPending.find((n) => (hevc ? hevcNalType(n) === 34 : avcNalType(n) === 8));
	const vps = configPending.find((n) => hevc && hevcNalType(n) === 32);
	if (!sps || !pps || (hevc && !vps)) throw new Error(t("log.videoUnsupported"));

	const description = hevc ? buildHvcC(vps, sps, pps) : buildAvcC(sps, pps);
	const descBuf = description.buffer.slice(description.byteOffset, description.byteOffset + description.byteLength);
	const candidates = hevc
		? ["hev1.1.6.L153.B0", "hvc1.1.6.L153.B0", "hev1.1.6.L123.B0"]
		: [avcMimeFromSps(sps), "avc1.64002a", "avc1.640028"];
	const hw = hwPreference();
	let chosen = null;
	for (const mime of candidates) {
		const cfg = { codec: mime, description: descBuf, optimizeForLatency: true, hardwareAcceleration: hw };
		if (await codecConfigSupported(cfg)) {
			chosen = cfg;
			break;
		}
	}
	if (!chosen) {
		for (const mime of candidates) {
			const cfg = { codec: mime, optimizeForLatency: true, hardwareAcceleration: hw };
			if (await codecConfigSupported(cfg)) {
				chosen = cfg;
				break;
			}
		}
	}
	if (!chosen)
		chosen = { codec: candidates[0], description: descBuf, optimizeForLatency: true, hardwareAcceleration: hw };

	const spsSize = hevc ? parseHevcSpsSize(sps) : parseAvcSpsSize(sps);
	const [pw, ph] = streamPresetSize();
	const codedW = spsSize?.width || pw;
	const codedH = spsSize?.height || ph;
	chosen.codedWidth = codedW;
	chosen.codedHeight = codedH;

	decoderMime = chosen.codec;
	decoder = new VideoDecoder({
		output: (frame) => {
			const d = videoFrameDest(frame);
			if (canvas.width !== d.sw || canvas.height !== d.sh) {
				canvas.width = d.sw;
				canvas.height = d.sh;
				fitVideoToStage();
			}
			try {
				ctx2d.drawImage(frame, d.sx, d.sy, d.sw, d.sh, 0, 0, canvas.width, canvas.height);
			} catch {
				ctx2d.drawImage(frame, 0, 0, canvas.width, canvas.height);
			}
			try { pushShareFrame(frame); } catch {}
			frame.close();
		},
		error: (err) => {
			const now = performance.now();
			if (now - lastDecodeErrAt > 2000) {
				lastDecodeErrAt = now;
				log("Video decoder: " + err.message, 0);
			}
			decoderReady = false;
			gotKeyframe = false;
			requestIdrThrottled();
		}
	});
	decoder.configure(chosen);
	decoderReady = true;
	log(t("log.videoCodec", { codec: decoderMime }));
}

function effectiveBitrate() {
	const res = Number(settings.resolution) || 4;
	const fps = Number(settings.fps) || 60;
	const caps = { 1: 5000, 2: 9000, 3: 12000, 4: 15000 };
	let cap = caps[res] || 15000;
	if (fps <= 30) cap = Math.round(cap * 0.85);
	const requested = Number(settings.bitrate) || cap;
	return Math.max(2000, Math.min(requested, cap));
}

function avMode() {
	return String(settings.av || "0");
}

function audioDisabled() {
	const m = avMode();
	return m === "1" || m === "3";
}

function videoDisabled() {
	const m = avMode();
	return m === "2" || m === "3";
}

async function pushVideo(ptr, size, lost, recovered) {
	if (!size || decoderFailed || !streaming || videoDisabled()) return;
	if (lost > 0 && !recovered) {
		gotKeyframe = false;
		requestIdrThrottled();
	}
	const sample = Module.HEAPU8.slice(ptr, ptr + size);
	const hevc = codec === 1;
	const nals = parseAnnexB(sample);
	if (!nals.length) return;
	collectConfig(nals, hevc);
	if (!decoderReady) {
		if (!hasParamSets(hevc)) {
			requestIdrThrottled();
			return;
		}
		if (!decoderSetup) {
			decoderSetup = configureDecoder().catch((e) => {
				decoderFailed = true;
				decoderSetup = null;
				log(e.message || t("log.videoUnsupported"), 0);
			});
		}
		await decoderSetup;
		if (!decoderReady) return;
	}
	const vcl = nals.filter((n) => !isConfigNal(n, hevc) && !isAudNal(n, hevc));
	if (!vcl.length) return;
	const key = vcl.some((n) => isKeyNal(n, hevc));
	if (!key && !gotKeyframe) {
		requestIdrThrottled();
		return;
	}
	if (decoder && decoder.decodeQueueSize > 3 && !key) return;
	try {
		const step = Math.round(1e6 / Math.max(30, Number(settings.fps) || 60));
		videoTs += step;
		decoder.decode(new EncodedVideoChunk({
			type: key ? "key" : "delta",
			timestamp: videoTs,
			data: packLengthPrefixed(vcl)
		}));
		if (key) gotKeyframe = true;
	} catch (e) {
		const now = performance.now();
		if (now - lastDecodeErrAt > 2000) {
			lastDecodeErrAt = now;
			log("decode: " + e.message, 0);
		}
		requestIdrThrottled();
	}
}

function audioBufMs() {
	const v = Number(settings.audiobuf);
	if (!Number.isFinite(v) || v <= 0) return 80;
	return Math.max(50, Math.min(160, v));
}

function audioTargetSamples() {
	return Math.round((audio.rate || 48000) * audioBufMs() / 1000);
}

function audioMaxSamples() {
	return Math.round(audioTargetSamples() * 2.2);
}

function pushAudioCfg() {
	if (!audio.port) return;
	try {
		audio.port.postMessage({ type: "cfg", target: audioTargetSamples(), max: audioMaxSamples() });
	} catch {}
}

function pushAudio(ptr, samples) {
	if (audioDisabled()) return;
	ensureAudioOut();
	if (!audio.ctx) return;
	if (audio.ctx.state === "suspended") audio.ctx.resume().catch(() => {});
	const n = samples * audio.channels;
	const pcm = Module.HEAP16.slice(ptr / 2, ptr / 2 + n);
	if (audio.port) {
		audio.port.postMessage({ type: "pcm16", pcm, channels: audio.channels }, [pcm.buffer]);
		return;
	}
	const planes = [];
	for (let ch = 0; ch < audio.channels; ch++) {
		const dest = new Float32Array(samples);
		for (let i = 0; i < samples; i++) dest[i] = pcm[i * audio.channels + ch] / 32768;
		planes.push(dest);
	}
	if (audio.proc) {
		enqueueAudioRing(planes, samples);
		return;
	}
	if (!audio.pending) audio.pending = [];
	audio.pending.push(planes);
	let queued = 0;
	for (const p of audio.pending) queued += p[0].length;
	const max = audioMaxSamples();
	while (audio.pending.length > 1 && queued > max) {
		queued -= audio.pending[0][0].length;
		audio.pending.shift();
	}
}

function enqueueAudioRing(planes, samples) {
	if (!audio.ring) audio.ring = [];
	audio.ring.push({ ch: planes, samples });
	let queued = 0;
	for (const f of audio.ring) queued += f.samples;
	const max = audioMaxSamples();
	while (audio.ring.length > 1 && queued > max) {
		queued -= audio.ring[0].samples;
		audio.ring.shift();
		audio.rpos = 0;
	}
}

function flushAudioPending() {
	if (!audio.pending?.length) return;
	const pending = audio.pending;
	audio.pending = [];
	if (audio.port) {
		for (const planes of pending)
			audio.port.postMessage({ type: "pcm", planes }, planes.map((p) => p.buffer));
		pushAudioCfg();
		return;
	}
	for (const planes of pending)
		enqueueAudioRing(planes, planes[0].length);
}

const AUDIO_WORKLET = `
class ChiakiOut extends AudioWorkletProcessor {
	constructor() {
		super();
		this.q = [];
		this.pos = 0;
		this.queued = 0;
		this.started = false;
		this.target = sampleRate * 0.08;
		this.max = sampleRate * 0.18;
		this.port.onmessage = (e) => {
			const m = e.data;
			if (!m) return;
			if (m.type === "clear") {
				this.q = [];
				this.pos = 0;
				this.queued = 0;
				this.started = false;
				return;
			}
			if (m.type === "cfg") {
				if (m.target > 0) this.target = m.target;
				if (m.max > 0) this.max = m.max;
				return;
			}
			let planes = m.planes;
			if (m.type === "pcm16" && m.pcm) {
				const ch = Math.max(1, m.channels || 2);
				const pcm = m.pcm;
				const samples = (pcm.length / ch) | 0;
				planes = [];
				for (let c = 0; c < ch; c++) {
					const dest = new Float32Array(samples);
					for (let i = 0; i < samples; i++) dest[i] = pcm[i * ch + c] * 3.0517578125e-5;
					planes.push(dest);
				}
			}
			if ((m.type === "pcm" || m.type === "pcm16") && planes && planes[0]) {
				const n = planes[0].length;
				this.q.push(planes);
				this.queued += n;
				while (this.q.length > 1 && this.queued > this.max) {
					this.queued -= this.q[0][0].length;
					this.q.shift();
					this.pos = 0;
				}
			}
		};
	}
	process(_inputs, outputs) {
		const out = outputs[0];
		if (!out || !out[0]) return true;
		const chs = out.length;
		const n = out[0].length;
		if (!this.started) {
			if (this.queued < this.target) {
				for (let c = 0; c < chs; c++) out[c].fill(0);
				return true;
			}
			this.started = true;
		}
		for (let i = 0; i < n; i++) {
			if (!this.q.length) {
				this.started = false;
				for (let c = 0; c < chs; c++) out[c].fill(0, i);
				break;
			}
			const f = this.q[0];
			for (let c = 0; c < chs; c++) out[c][i] = f[Math.min(c, f.length - 1)][this.pos];
			this.pos++;
			if (this.pos >= f[0].length) {
				this.queued -= f[0].length;
				this.q.shift();
				this.pos = 0;
			}
		}
		return true;
	}
}
registerProcessor("chiaki-out", ChiakiOut);
`;

function ensureAudioOut() {
	if (audio.ctx && (audio.port || audio.proc)) return;
	if (!audio.ctx) {
		audio.ctx = new AudioContext({ sampleRate: audio.rate || 48000, latencyHint: "interactive" });
		audio.gain = audio.ctx.createGain();
		audio.gain.connect(audio.ctx.destination);
		if (share.active && !share.isGuest && share.pcs.size) {
			const hadAudio = !!share.audioDest;
			attachShareAudio();
			if (!hadAudio && share.audioDest) shareSyncPeerTracks();
		}
	}
	if (audio.gain && share.audioDest) syncShareAudioDelay();
	if (audio.workletTried) return;
	audio.workletTried = true;
	const startWorklet = async () => {
		if (!audio.ctx.audioWorklet) throw new Error("no worklet");
		const url = URL.createObjectURL(new Blob([AUDIO_WORKLET], { type: "text/javascript" }));
		try {
			await audio.ctx.audioWorklet.addModule(url);
			const node = new AudioWorkletNode(audio.ctx, "chiaki-out", {
				numberOfInputs: 0,
				numberOfOutputs: 1,
				outputChannelCount: [audio.channels || 2]
			});
			node.connect(audio.gain);
			audio.node = node;
			audio.port = node.port;
			pushAudioCfg();
			flushAudioPending();
		} finally {
			URL.revokeObjectURL(url);
		}
	};
	startWorklet().catch(() => {
		audio.port = null;
		flushAudioPending();
		ensureScriptAudio();
	});
}

function ensureScriptAudio() {
	if (audio.proc || !audio.ctx) return;
	if (!audio.ring) audio.ring = [];
	audio.rpos = audio.rpos || 0;
	const size = 1024;
	const proc = audio.ctx.createScriptProcessor(size, 0, audio.channels || 2);
	proc.onaudioprocess = (e) => {
		const chs = e.outputBuffer.numberOfChannels;
		const n = e.outputBuffer.length;
		const outs = [];
		for (let c = 0; c < chs; c++) outs.push(e.outputBuffer.getChannelData(c));
		let i = 0;
		while (i < n) {
			if (!audio.ring.length) {
				for (let c = 0; c < chs; c++) outs[c].fill(0, i);
				break;
			}
			const frame = audio.ring[0];
			const take = Math.min(frame.samples - audio.rpos, n - i);
			for (let c = 0; c < chs; c++)
				outs[c].set(frame.ch[Math.min(c, frame.ch.length - 1)].subarray(audio.rpos, audio.rpos + take), i);
			audio.rpos += take;
			i += take;
			if (audio.rpos >= frame.samples) {
				audio.ring.shift();
				audio.rpos = 0;
			}
		}
	};
	proc.connect(audio.gain);
	audio.proc = proc;
}

function resetAudioOut() {
	if (audio.port) {
		try { audio.port.postMessage({ type: "clear" }); } catch {}
	}
	audio.pending = [];
	audio.ring = [];
	audio.rpos = 0;
	audio.next = audio.ctx ? audio.ctx.currentTime : 0;
}

function clamp(v, min, max) {
	return Math.max(min, Math.min(max, v));
}

function stick(v) {
	return Math.max(-32768, Math.min(32767, Math.round(v * 32767)));
}

function deadzone(v, z) {
	return Math.abs(v) < (z || 0.15) ? 0 : v;
}

function axisAt(gp, i) {
	const v = gp?.axes?.[i];
	return Number.isFinite(v) ? v : 0;
}

function padStickAxes(gp) {
	const lx = axisAt(gp, 0);
	const ly = axisAt(gp, 1);
	let rx = axisAt(gp, 2);
	let ry = axisAt(gp, 3);
	if (gp.mapping !== "standard" && (gp.axes?.length || 0) >= 6) {
		rx = axisAt(gp, 2);
		ry = axisAt(gp, 5);
	} else if (Math.abs(rx) < 0.08 && Math.abs(ry) < 0.08 && (gp.axes?.length || 0) >= 6) {
		const altX = axisAt(gp, 4);
		const altY = axisAt(gp, 5);
		if (Math.abs(altX) > 0.12 || Math.abs(altY) > 0.12) {
			rx = altX;
			ry = altY;
		}
	}
	return { lx, ly, rx, ry };
}

let activePadIndex = -1;

function listGamepads() {
	const raw = navigator.getGamepads ? navigator.getGamepads() : [];
	const out = [];
	for (let i = 0; i < raw.length; i++) {
		const gp = raw[i];
		if (gp && gp.connected && (gp.buttons?.length || 0) >= 8)
			out.push(gp);
	}
	return out;
}

function scorePad(gp) {
	const id = String(gp.id || "").toLowerCase();
	if (/dualshock|dualsense|wireless controller|sony|playstation|ps[45]|ds4|ds5/.test(id))
		return 4;
	if (gp.mapping === "standard") return 3;
	if (/xinput|xbox/.test(id)) return 2;
	return 1;
}

function pickGamepad() {
	const pads = listGamepads();
	if (!pads.length) {
		activePadIndex = -1;
		return null;
	}
	const kept = pads.find((p) => p.index === activePadIndex);
	if (kept) return kept;
	pads.sort((a, b) => scorePad(b) - scorePad(a) || a.index - b.index);
	activePadIndex = pads[0].index;
	return pads[0];
}

function refreshPadStatus() {
	const gp = pickGamepad();
	const el = $("pad-status");
	if (el) el.textContent = gp ? (gp.id || t("controllers.connected")) : t("controllers.none");
	return gp;
}

function onGamepadHotplug(ev) {
	if (ev.type === "gamepadconnected" && ev.gamepad)
		activePadIndex = ev.gamepad.index;
	else if (ev.type === "gamepaddisconnected" && ev.gamepad?.index === activePadIndex)
		activePadIndex = -1;
	try { navigator.getGamepads?.(); } catch {}
	refreshPadStatus();
}

function sleep(ms) {
	return new Promise((r) => setTimeout(r, ms));
}

const VPAD_ITEMS = [
	{ id: "l2", kind: "trigger", axis: "l2", label: "L2", cls: "vpad-sh" },
	{ id: "r2", kind: "trigger", axis: "r2", label: "R2", cls: "vpad-sh" },
	{ id: "l1", kind: "button", bit: "L1", label: "L1", cls: "vpad-sh" },
	{ id: "r1", kind: "button", bit: "R1", label: "R1", cls: "vpad-sh" },
	{ id: "share", kind: "button", bit: "SHARE", label: "Share", cls: "vpad-wide" },
	{ id: "options", kind: "button", bit: "OPTIONS", label: "Opt", cls: "vpad-wide" },
	{ id: "touchpad", kind: "button", bit: "TOUCHPAD", label: "Touch", cls: "vpad-wide" },
	{ id: "ls", kind: "stick", axes: ["lx", "ly"], label: "L" },
	{ id: "up", kind: "button", bit: "UP", label: "▲" },
	{ id: "left", kind: "button", bit: "LEFT", label: "◀" },
	{ id: "right", kind: "button", bit: "RIGHT", label: "▶" },
	{ id: "down", kind: "button", bit: "DOWN", label: "▼" },
	{ id: "pyramid", kind: "button", bit: "PYRAMID", label: "△" },
	{ id: "box", kind: "button", bit: "BOX", label: "□" },
	{ id: "moon", kind: "button", bit: "MOON", label: "○" },
	{ id: "cross", kind: "button", bit: "CROSS", label: "✕" },
	{ id: "rs", kind: "stick", axes: ["rx", "ry"], label: "R" },
	{ id: "ps", kind: "button", bit: "PS", label: "PS" },
	{ id: "l3", kind: "button", bit: "L3", label: "L3" },
	{ id: "r3", kind: "button", bit: "R3", label: "R3" }
];

const VPAD_SHARE_GROUPS = [
	{ id: "face", keys: ["cross", "moon", "box", "pyramid"] },
	{ id: "dpad", keys: ["up", "down", "left", "right"] },
	{ id: "shoulders", keys: ["l1", "r1", "l2", "r2"] },
	{ id: "lstick", keys: ["ls", "l3"] },
	{ id: "rstick", keys: ["rs", "r3"] },
	{ id: "system", keys: ["options", "share", "touchpad", "ps"] }
];

const VPAD_DEFAULTS = {
	l2: { x: 10, y: 9 }, r2: { x: 90, y: 9 },
	l1: { x: 10, y: 20 }, r1: { x: 90, y: 20 },
	share: { x: 34, y: 18 }, options: { x: 66, y: 18 },
	touchpad: { x: 50, y: 10 },
	ls: { x: 20, y: 42 },
	up: { x: 18, y: 62 }, left: { x: 10, y: 72 }, right: { x: 26, y: 72 }, down: { x: 18, y: 82 },
	pyramid: { x: 82, y: 50 }, box: { x: 73, y: 62 }, moon: { x: 91, y: 62 }, cross: { x: 82, y: 74 },
	rs: { x: 70, y: 84 },
	ps: { x: 50, y: 90 },
	l3: { x: 8, y: 90 }, r3: { x: 92, y: 90 }
};

const VPAD_THEMES = [
	{ id: "classic", nameKey: "vpad.theme.classic" },
	{ id: "dualsense", nameKey: "vpad.theme.dualsense" },
	{ id: "xbox", nameKey: "vpad.theme.xbox" },
	{ id: "outline", nameKey: "vpad.theme.outline" },
	{ id: "neon", nameKey: "vpad.theme.neon" }
];

let vpadEditSelected = null;

function vpadThemeId() {
	const id = String(settings.vpadTheme || "classic");
	return VPAD_THEMES.some((t) => t.id === id) ? id : "classic";
}

function activeVpadThemeId() {
	if (share.isGuest) {
		const id = String(share.vpadTheme || "classic");
		return VPAD_THEMES.some((t) => t.id === id) ? id : "classic";
	}
	return vpadThemeId();
}

function vpadCustomMap() {
	if (share.isGuest)
		return share.vpadCustom && typeof share.vpadCustom === "object" ? share.vpadCustom : {};
	return settings.vpadCustom && typeof settings.vpadCustom === "object" ? settings.vpadCustom : {};
}

function vpadCustomOf(id) {
	const url = vpadCustomMap()[id];
	return typeof url === "string" && url.startsWith("data:image/") ? url : "";
}

function vpadSvg(inner, color) {
	return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32" class="vpad-glyph" aria-hidden="true">${inner}</svg>`;
}

function vpadThemeGlyph(item) {
	const theme = activeVpadThemeId();
	const id = item.id;
	if (theme === "xbox") {
		if (id === "cross") return `<span class="vpad-letter" style="color:#3ddc84">A</span>`;
		if (id === "moon") return `<span class="vpad-letter" style="color:#ff5a5a">B</span>`;
		if (id === "box") return `<span class="vpad-letter" style="color:#4aa3ff">X</span>`;
		if (id === "pyramid") return `<span class="vpad-letter" style="color:#ffd54f">Y</span>`;
	}
	if (theme === "dualsense" || theme === "outline" || theme === "neon") {
		if (id === "cross") return vpadSvg(`<path d="M9 9 L23 23 M23 9 L9 23" fill="none" stroke="currentColor" stroke-width="3.2" stroke-linecap="round"/>`, "");
		if (id === "moon") return vpadSvg(`<circle cx="16" cy="16" r="8" fill="none" stroke="currentColor" stroke-width="3"/>`, "");
		if (id === "box") return vpadSvg(`<rect x="8" y="8" width="16" height="16" rx="2" fill="none" stroke="currentColor" stroke-width="2.8"/>`, "");
		if (id === "pyramid") return vpadSvg(`<path d="M16 7 L26 25 H6 Z" fill="none" stroke="currentColor" stroke-width="2.6" stroke-linejoin="round"/>`, "");
	}
	return "";
}

function vpadInnerHtml(item) {
	const custom = vpadCustomOf(item.id);
	if (custom) return `<img class="vpad-face" alt="" src="${custom}">`;
	if (item.kind === "stick") return "";
	const glyph = vpadThemeGlyph(item);
	if (glyph) return glyph;
	return `<span class="vpad-label">${item.label}</span>`;
}

function fillVpadThemePickers() {
	const html = VPAD_THEMES.map((th) => (
		`<button type="button" class="vpad-theme-chip" data-theme="${th.id}">` +
		`<span class="vpad-theme-dots" data-theme="${th.id}" aria-hidden="true"></span>` +
		`<span data-i18n="${th.nameKey}">${th.id}</span></button>`
	)).join("");
	["vpad-theme-picker", "vpad-theme-picker-editor"].forEach((id) => {
		const el = $(id);
		if (!el) return;
		el.innerHTML = html;
		el.querySelectorAll(".vpad-theme-chip").forEach((btn) => {
			btn.onclick = () => setVpadTheme(btn.dataset.theme);
		});
	});
	syncVpadThemeChips();
}

function syncVpadThemeChips() {
	const cur = vpadThemeId();
	document.querySelectorAll(".vpad-theme-chip").forEach((btn) => {
		btn.classList.toggle("active", btn.dataset.theme === cur);
	});
}

function setVpadTheme(id) {
	settings.vpadTheme = VPAD_THEMES.some((t) => t.id === id) ? id : "classic";
	localStorage.setItem(SETTINGS_KEY, JSON.stringify(settings));
	scheduleCloudPush();
	syncVpadThemeChips();
	renderVpad($("vpad"), false);
	renderVpad($("vpad-editor-stage"), true);
	shareBroadcastVpadSkin();
}

function syncVpadCustomPanel() {
	const id = vpadEditSelected;
	const name = $("vpad-custom-name");
	const pick = $("btn-vpad-custom");
	const clear = $("btn-vpad-custom-clear");
	if (!name || !pick || !clear) return;
	if (!id) {
		name.dataset.i18n = "vpad.customIdle";
		name.textContent = t("vpad.customIdle");
		pick.disabled = true;
		clear.disabled = true;
		return;
	}
	const item = VPAD_ITEMS.find((x) => x.id === id);
	name.removeAttribute("data-i18n");
	name.textContent = t("vpad.customFor", { name: item ? item.label : id });
	pick.disabled = false;
	clear.disabled = !vpadCustomOf(id);
}

async function setVpadCustomImage(file) {
	if (!vpadEditSelected || !file || !file.type.startsWith("image/")) return;
	const url = URL.createObjectURL(file);
	try {
		const img = await new Promise((resolve, reject) => {
			const el = new Image();
			el.onload = () => resolve(el);
			el.onerror = reject;
			el.src = url;
		});
		const c = document.createElement("canvas");
		c.width = 96;
		c.height = 96;
		c.getContext("2d").drawImage(img, 0, 0, 96, 96);
		const data = c.toDataURL("image/png");
		settings.vpadCustom = { ...(settings.vpadCustom || {}), [vpadEditSelected]: data };
		localStorage.setItem(SETTINGS_KEY, JSON.stringify(settings));
		scheduleCloudPush();
		renderVpad($("vpad"), false);
		renderVpad($("vpad-editor-stage"), true);
		syncVpadCustomPanel();
		shareBroadcastVpadSkin();
	} finally {
		URL.revokeObjectURL(url);
	}
}

function clearVpadCustomImage() {
	if (!vpadEditSelected) return;
	const next = { ...(settings.vpadCustom || {}) };
	delete next[vpadEditSelected];
	settings.vpadCustom = next;
	localStorage.setItem(SETTINGS_KEY, JSON.stringify(settings));
	scheduleCloudPush();
	renderVpad($("vpad"), false);
	renderVpad($("vpad-editor-stage"), true);
	syncVpadCustomPanel();
	shareBroadcastVpadSkin();
}

function vpadOpacityPct() {
	const raw = Number(settings.vpadOpacity);
	return clamp(Number.isFinite(raw) ? raw : 55, 1, 100);
}

function activeVpadOpacityPct() {
	if (share.isGuest && Number.isFinite(Number(share.vpadOpacity)))
		return clamp(Number(share.vpadOpacity), 1, 100);
	return vpadOpacityPct();
}

function applyVpadOpacity() {
	const pct = activeVpadOpacityPct();
	if (!share.isGuest) settings.vpadOpacity = pct;
	const alpha = String(pct / 100);
	document.documentElement.style.setProperty("--vpad-alpha", alpha);
	["vpad", "vpad-editor-stage"].forEach((id) => {
		const el = $(id);
		if (el) el.style.setProperty("--vpad-alpha", alpha);
	});
	const out = $("vpad-editor-opacity-val");
	if (out) out.textContent = pct + " %";
	const slider = $("vpad-editor-opacity");
	if (slider && slider.value !== String(pct)) slider.value = String(pct);
}

function vpadBaseSize(item) {
	if (item.kind === "stick") return { w: 88, h: 88, font: 13, knob: 40 };
	if (item.cls === "vpad-wide") return { w: 88, h: 36, font: 11, knob: 0 };
	if (item.cls === "vpad-sh") return { w: 64, h: 36, font: 12, knob: 0 };
	return { w: 52, h: 52, font: 13, knob: 0 };
}

function vpadLayoutMap() {
	if (share.isGuest)
		return share.vpadLayout && typeof share.vpadLayout === "object" ? share.vpadLayout : {};
	return settings.vpadLayout && typeof settings.vpadLayout === "object" ? settings.vpadLayout : {};
}

function vpadLayoutOf(id) {
	const fallback = VPAD_DEFAULTS[id] || { x: 50, y: 50 };
	const cur = vpadLayoutMap()[id] || {};
	return {
		x: Number.isFinite(cur.x) ? cur.x : fallback.x,
		y: Number.isFinite(cur.y) ? cur.y : fallback.y,
		scale: clamp(Number.isFinite(cur.scale) ? cur.scale : 1, 0.45, 3)
	};
}

function patchVpadLayout(id, patch) {
	if (share.isGuest) return vpadLayoutOf(id);
	const next = { ...vpadLayoutOf(id), ...patch };
	settings.vpadLayout = { ...(settings.vpadLayout || {}), [id]: next };
	localStorage.setItem(SETTINGS_KEY, JSON.stringify(settings));
	shareBroadcastVpadSkin(250);
	return next;
}

function resetVpadLayout() {
	if (share.isGuest) return;
	settings.vpadLayout = {};
	localStorage.setItem(SETTINGS_KEY, JSON.stringify(settings));
	renderVpad($("vpad"), false);
	renderVpad($("vpad-editor-stage"), true);
	shareBroadcastVpadSkin();
}

function applyVpadStyle(el, item) {
	const p = vpadLayoutOf(item.id);
	const base = vpadBaseSize(item);
	el.style.left = p.x + "%";
	el.style.top = p.y + "%";
	el.style.width = (base.w * p.scale) + "px";
	el.style.height = (base.h * p.scale) + "px";
	el.style.fontSize = Math.max(9, base.font * p.scale) + "px";
	const knob = el.querySelector(".vpad-knob");
	if (knob && base.knob) {
		knob.style.width = (base.knob * p.scale) + "px";
		knob.style.height = (base.knob * p.scale) + "px";
	}
}

function renderVpad(root, edit) {
	if (!root) return;
	root.innerHTML = "";
	if (edit) root.classList.add("vpad-edit");
	else root.classList.remove("vpad-edit");
	root.dataset.theme = activeVpadThemeId();
	root.style.setProperty("--vpad-alpha", String(activeVpadOpacityPct() / 100));
	for (const item of VPAD_ITEMS) {
		if (!edit && !vpadKeyAllowed(item.id)) continue;
		const el = document.createElement("div");
		el.setAttribute("role", "button");
		el.dataset.vpadId = item.id;
		el.className = (item.kind === "stick" ? "vpad-stick" : "vpad-btn") + (item.cls ? " " + item.cls : "");
		if (vpadCustomOf(item.id)) el.classList.add("has-face");
		el.innerHTML = vpadInnerHtml(item);
		if (item.kind === "stick") {
			const knob = document.createElement("span");
			knob.className = "vpad-knob";
			el.appendChild(knob);
		}
		applyVpadStyle(el, item);
		root.appendChild(el);
		if (edit) bindVpadDrag(el, item, root);
		else bindVpadPlay(el, item);
	}
	if (edit && vpadEditSelected) {
		const sel = root.querySelector(`[data-vpad-id="${vpadEditSelected}"]`);
		if (sel) selectVpadEdit(root, sel);
	}
}

function selectVpadEdit(root, el) {
	root.querySelectorAll(".vpad-btn, .vpad-stick").forEach((n) => n.classList.toggle("selected", n === el));
	vpadEditSelected = el?.dataset?.vpadId || null;
	syncVpadCustomPanel();
}

function bindVpadDrag(el, item, root) {
	const handle = document.createElement("span");
	handle.className = "vpad-resize";
	el.appendChild(handle);

	const startMove = (ev) => {
		ev.preventDefault();
		el.setPointerCapture(ev.pointerId);
		selectVpadEdit(root, el);
		const move = (e) => {
			const r = root.getBoundingClientRect();
			if (!r.width || !r.height) return;
			const x = clamp(((e.clientX - r.left) / r.width) * 100, 2, 98);
			const y = clamp(((e.clientY - r.top) / r.height) * 100, 2, 98);
			patchVpadLayout(item.id, { x: Math.round(x * 10) / 10, y: Math.round(y * 10) / 10 });
			applyVpadStyle(el, item);
		};
		const up = () => {
			el.onpointermove = null;
			el.onpointerup = null;
			el.onpointercancel = null;
			renderVpad($("vpad"), false);
		};
		el.onpointermove = move;
		el.onpointerup = up;
		el.onpointercancel = up;
	};

	handle.onpointerdown = (ev) => {
		ev.preventDefault();
		ev.stopPropagation();
		handle.setPointerCapture(ev.pointerId);
		selectVpadEdit(root, el);
		const start = vpadLayoutOf(item.id);
		const origin = ev.clientX + ev.clientY;
		const resize = (e) => {
			const delta = ((e.clientX + e.clientY) - origin) / 90;
			const scale = clamp(start.scale + delta, 0.45, 3);
			patchVpadLayout(item.id, { scale: Math.round(scale * 20) / 20 });
			applyVpadStyle(el, item);
		};
		const up = () => {
			handle.onpointermove = null;
			handle.onpointerup = null;
			handle.onpointercancel = null;
			renderVpad($("vpad"), false);
		};
		handle.onpointermove = resize;
		handle.onpointerup = up;
		handle.onpointercancel = up;
	};

	el.onpointerdown = (ev) => {
		if (ev.target.closest(".vpad-resize")) return;
		startMove(ev);
	};
	el.addEventListener("wheel", (ev) => {
		ev.preventDefault();
		selectVpadEdit(root, el);
		const cur = vpadLayoutOf(item.id);
		const scale = clamp(cur.scale + (ev.deltaY > 0 ? -0.1 : 0.1), 0.45, 3);
		patchVpadLayout(item.id, { scale: Math.round(scale * 20) / 20 });
		applyVpadStyle(el, item);
		renderVpad($("vpad"), false);
	}, { passive: false });
}

function updateVpadStick(ev, st) {
	const r = st.el.getBoundingClientRect();
	const radius = Math.max(14, Math.min(r.width, r.height) / 2 - 6);
	let dx = (ev.clientX - (r.left + r.width / 2)) / radius;
	let dy = (ev.clientY - (r.top + r.height / 2)) / radius;
	const mag = Math.hypot(dx, dy);
	if (mag > 1) {
		dx /= mag;
		dy /= mag;
	}
	vpad[st.item.axes[0]] = stick(dx);
	vpad[st.item.axes[1]] = stick(dy);
	const knob = st.el.querySelector(".vpad-knob");
	if (knob) {
		const travel = Math.min(r.width, r.height) * 0.22;
		knob.style.transform = `translate(${dx * travel}px, ${dy * travel}px)`;
	}
}

function releaseVpadStick(st) {
	vpad[st.item.axes[0]] = 0;
	vpad[st.item.axes[1]] = 0;
	const knob = st.el.querySelector(".vpad-knob");
	if (knob) knob.style.transform = "translate(0px, 0px)";
	st.el.classList.remove("active");
}

function setVpadControl(item, el, on) {
	if (el) el.classList.toggle("active", on);
	if (item.kind === "button") {
		if (on) vpad.buttons |= BTN[item.bit];
		else vpad.buttons &= ~BTN[item.bit];
	} else if (item.kind === "trigger") {
		vpad[item.axis] = on ? 255 : 0;
	}
}

function flushPadNow() {
	lastPadSent = "";
	pollInput();
}

const VPAD_MIN_PRESS_MS = 90;
const vpadReleaseTimers = new Map();

function onVpadPointerMove(ev) {
	const st = vpadPtrs.get(ev.pointerId);
	if (!st) return;
	ev.preventDefault();
	updateVpadStick(ev, st);
}

function onVpadPointerUp(ev) {
	const held = vpadHeld.get(ev.pointerId);
	if (!held) return;
	vpadHeld.delete(ev.pointerId);
	if (held.kind === "stick") {
		const st = vpadPtrs.get(ev.pointerId);
		if (st) releaseVpadStick(st);
		vpadPtrs.delete(ev.pointerId);
		flushPadNow();
		return;
	}
	const wait = Math.max(0, VPAD_MIN_PRESS_MS - (performance.now() - held.at));
	const prev = vpadReleaseTimers.get(held.item.id);
	if (prev) clearTimeout(prev);
	const release = () => {
		vpadReleaseTimers.delete(held.item.id);
		setVpadControl(held.item, held.el, false);
		flushPadNow();
	};
	if (wait > 0) vpadReleaseTimers.set(held.item.id, setTimeout(release, wait));
	else release();
}

function vpadStickActive(id) {
	for (const st of vpadPtrs.values()) {
		if (st.item.id === id) return true;
	}
	if (id === "ls") return !!(vpad.lx || vpad.ly);
	if (id === "rs") return !!(vpad.rx || vpad.ry);
	return false;
}

function bindVpadPlay(el, item) {
	const down = (ev) => {
		if (!vpadOn) return;
		if (ev.pointerType === "mouse" && ev.button !== 0) return;
		if (vpadHeld.has(ev.pointerId)) return;
		ev.preventDefault();
		ev.stopPropagation();
		const pending = vpadReleaseTimers.get(item.id);
		if (pending) {
			clearTimeout(pending);
			vpadReleaseTimers.delete(item.id);
		}
		vpadHeld.set(ev.pointerId, { item, el, kind: item.kind, at: performance.now() });
		try { el.setPointerCapture(ev.pointerId); } catch {}
		if (item.kind === "stick") {
			vpadPtrs.set(ev.pointerId, { item, el });
			el.classList.add("active");
			updateVpadStick(ev, vpadPtrs.get(ev.pointerId));
			flushPadNow();
			return;
		}
		setVpadControl(item, el, true);
		flushPadNow();
	};
	el.addEventListener("pointerdown", down, { passive: false });
	el.addEventListener("contextmenu", (e) => e.preventDefault());
}

function syncVpadUi() {
	const guestKeys = !share.isGuest || !Array.isArray(share.vpadKeys) || share.vpadKeys.length > 0;
	vpadOn = share.isGuest ? (!!share.vpad && guestKeys) : !!settings.vpadEnabled;
	const btn = $("btn-vpad");
	if (btn) btn.setAttribute("aria-pressed", vpadOn ? "true" : "false");
	const pad = $("vpad");
	if (!pad) return;
	pad.classList.toggle("hidden", !vpadOn);
	pad.setAttribute("aria-hidden", vpadOn ? "false" : "true");
	applyVpadOpacity();
	if (vpadOn) renderVpad(pad, false);
	syncPointerLock();
}

function openVpadEditor() {
	$("vpad-editor-modal").classList.remove("hidden");
	applyVpadOpacity();
	renderVpad($("vpad-editor-stage"), true);
}

function closeVpadEditor() {
	$("vpad-editor-modal").classList.add("hidden");
	saveSettings();
	syncVpadUi();
	shareBroadcastVpadSkin();
}

function pollInput() {
	const gp = refreshPadStatus();
	if (!streaming) return;
	if (typeof api.sessionSetController !== "function") return;
	let buttons = 0, l2 = 0, r2 = 0, lx = 0, ly = 0, rx = 0, ry = 0;
	if (gp) {
		if (gp.buttons[0]?.pressed) buttons |= BTN.CROSS;
		if (gp.buttons[1]?.pressed) buttons |= BTN.MOON;
		if (gp.buttons[2]?.pressed) buttons |= BTN.BOX;
		if (gp.buttons[3]?.pressed) buttons |= BTN.PYRAMID;
		if (gp.buttons[4]?.pressed) buttons |= BTN.L1;
		if (gp.buttons[5]?.pressed) buttons |= BTN.R1;
		if (gp.buttons[8]?.pressed) buttons |= BTN.SHARE;
		if (gp.buttons[9]?.pressed) buttons |= BTN.OPTIONS;
		if (gp.buttons[10]?.pressed) buttons |= BTN.L3;
		if (gp.buttons[11]?.pressed) buttons |= BTN.R3;
		if (gp.buttons[12]?.pressed) buttons |= BTN.UP;
		if (gp.buttons[13]?.pressed) buttons |= BTN.DOWN;
		if (gp.buttons[14]?.pressed) buttons |= BTN.LEFT;
		if (gp.buttons[15]?.pressed) buttons |= BTN.RIGHT;
		if (gp.buttons[16]?.pressed) buttons |= BTN.PS;
		if (gp.buttons[17]?.pressed) buttons |= BTN.TOUCHPAD;
		l2 = Math.round((gp.buttons[6]?.value || 0) * 255);
		r2 = Math.round((gp.buttons[7]?.value || 0) * 255);
		const ax = padStickAxes(gp);
		lx = stick(deadzone(ax.lx));
		ly = stick(deadzone(ax.ly));
		rx = stick(deadzone(ax.rx));
		ry = stick(deadzone(ax.ry));
	}
	{
		for (const item of KEYMAP_ITEMS) {
			const held = (settings.keyboardEnabled !== false && codesOf(item.id, false).some(bindingHeld))
				|| codesOf(item.id, true).some(bindingHeld);
			if (!held) continue;
			if (item.kind === "button") buttons |= BTN[item.bit];
			else if (item.kind === "trigger") {
				if (item.axis === "l2") l2 = 255;
				if (item.axis === "r2") r2 = 255;
			} else if (item.kind === "stick") {
				const v = stick(item.dir);
				if (item.axis === "lx") lx = v;
				if (item.axis === "ly") ly = v;
				if (item.axis === "rx") rx = v;
				if (item.axis === "ry") ry = v;
			}
		}
	}
	if (settings.mouseStick === "ls" || settings.mouseStick === "rs") {
		mouseLook.x *= 0.86;
		mouseLook.y *= 0.86;
		if (Math.abs(mouseLook.x) < 0.02) mouseLook.x = 0;
		if (Math.abs(mouseLook.y) < 0.02) mouseLook.y = 0;
		const mx = stick(mouseLook.x);
		const my = stick(mouseLook.y);
		const mouseLive = document.pointerLockElement
			|| Math.abs(mouseLook.x) > 0.02
			|| Math.abs(mouseLook.y) > 0.02;
		if (mouseLive) {
			if (settings.mouseStick === "ls") {
				if (Math.abs(lx) <= 512 && Math.abs(ly) <= 512) { lx = mx; ly = my; }
			} else if (Math.abs(rx) <= 512 && Math.abs(ry) <= 512) {
				rx = mx;
				ry = my;
			}
		}
	}
	if (vpadOn) {
		buttons |= vpad.buttons;
		l2 = Math.max(l2, vpad.l2);
		r2 = Math.max(r2, vpad.r2);
		if (vpadStickActive("ls")) { lx = vpad.lx; ly = vpad.ly; }
		if (vpadStickActive("rs")) { rx = vpad.rx; ry = vpad.ry; }
	}
	({ buttons, l2, r2, lx, ly, rx, ry } = mergeGuestPad(buttons, l2, r2, lx, ly, rx, ry));
	const useTouch = settings.mouseTouchEnabled !== false
		&& settings.mouseStick === "none"
		&& !mouse0Mapped()
		&& !document.pointerLockElement
		&& touch.active;
	const padKey = `${buttons},${l2},${r2},${lx},${ly},${rx},${ry},${useTouch ? 1 : 0},${touch.x},${touch.y}`;
	if (padKey === lastPadSent) return;
	lastPadSent = padKey;
	api.sessionSetController(buttons, l2, r2, lx, ly, rx, ry, useTouch ? 1 : 0, touch.x, touch.y);
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

function normalizeState(state) {
	const v = String(state || "unknown").toLowerCase();
	if (v === "ready" || v === "standby" || v === "waking") return v;
	return "unknown";
}

function hostKey(h) {
	const addr = normAddr(h && (h.addr || h.host));
	if (h && h.manual && addr) return "addr:" + addr;
	const id = normHostId(h && h.id);
	if (id) return "id:" + id;
	return "addr:" + addr;
}

function hostsMatch(a, b) {
	if (!a || !b) return false;
	const ida = normHostId(a.id);
	const idb = normHostId(b.id);
	if (ida && idb && ida === idb) return true;
	return hostsMatchAddr(a, b);
}

function hostsMatchAddr(a, b) {
	if (!a || !b) return false;
	const aa = normAddr(a.addr || a.host);
	const ba = normAddr(b.addr || b.host);
	return !!(aa && ba && aa === ba);
}

function syncDocumentTitle() {
	if (currentView === "settings") {
		document.title = t("title.settings");
		return;
	}
	if (currentView === "stream") {
		const name = (activeHost && activeHost.name) || streamTitleName;
		document.title = name ? t("title.streamNamed", { name }) : t("title.stream");
		return;
	}
	document.title = t("title.welcome");
}

function scheduleRenderHosts() {
	if (hostsRenderTimer) return;
	hostsRenderTimer = requestAnimationFrame(() => {
		hostsRenderTimer = 0;
		renderHosts();
		renderSavedList();
	});
}

function restartDiscovery() {
	if (!api || !api.discoverStart) return;
	if (probeRunning) return;
	clearTimeout(discoveryRestartTimer);
	discoveryRestartTimer = setTimeout(() => {
		syncDiscoveryService();
	}, 400);
}

function findDiscoveryRow(addr, id) {
	const ip = normAddr(addr);
	if (ip) {
		const byAddr = discovered.find((d) => normAddr(d.addr) === ip);
		if (byAddr) return byAddr;
	}
	return discovered.find((d) => hostsMatch(d, { addr, id })) || null;
}

function applyDiscoveryRow(target, h) {
	if (!target.manual) target.addr = h.addr || target.addr;
	if (!target.name || target.name === target.addr)
		target.name = h.name || target.name;
	target.ps5 = !!h.ps5;
	target.id = h.id || target.id;
	target.state = normalizeState(h.state);
	target.appName = h.appName || "";
	target.discovered = true;
}

function mergedHosts() {
	const hidden = hiddenSet();
	const saved = savedHosts();
	const byKey = new Map();
	const isHidden = (addr) => hidden.has(addr) || hidden.has(normAddr(addr));

	for (const h of saved) {
		if (isHidden(h.host)) continue;
		const row = {
			addr: h.host,
			name: h.name || h.host,
			ps5: !!h.ps5,
			id: h.id || "",
			state: "unknown",
			appName: "",
			discovered: false,
			registered: !!(h.registKey && h.morning),
			registKey: h.registKey || "",
			morning: h.morning || "",
			psnId: h.psnId || "",
			manual: true
		};
		byKey.set("saved:" + (normAddr(h.host) || h.host), row);
	}
	for (const h of discovered) {
		if (isHidden(h.addr)) continue;
		const ip = normAddr(h.addr);
		let prev = ip ? byKey.get("saved:" + ip) : null;
		if (!prev) {
			for (const row of byKey.values()) {
				if (row.manual && hostsMatchAddr(row, h)) {
					prev = row;
					break;
				}
			}
		}
		if (prev) {
			applyDiscoveryRow(prev, h);
			continue;
		}
		if (!discoveryOn && !cloud.homeProxy) continue;
		byKey.set(hostKey(h), {
			addr: h.addr,
			name: h.name || h.addr,
			ps5: !!h.ps5,
			id: h.id || "",
			state: normalizeState(h.state),
			appName: h.appName || "",
			discovered: true,
			registered: false,
			registKey: "",
			morning: "",
			manual: false
		});
	}
	return [...byKey.values()];
}

function iconHtml(ps5, state, key) {
	const raw = ps5 ? svgIcons.ps5 : svgIcons.ps4;
	const cls = state === "ready" ? "ready" : (state === "standby" || state === "waking" ? "standby" : "unknown");
	const prefix = String(key || "h").replace(/[^a-zA-Z0-9_-]/g, "_");
	const svg = raw
		.replace(/id="([^"]+)"/g, `id="${prefix}-$1"`)
		.replace(/url\(#([^)]+)\)/g, `url(#${prefix}-$1)`);
	return `<div class="icon ${cls}">${svg}</div>`;
}

function escapeHtml(s) {
	return String(s ?? "")
		.replace(/&/g, "&amp;")
		.replace(/</g, "&lt;")
		.replace(/>/g, "&gt;")
		.replace(/"/g, "&quot;");
}

function askConfirm(title, text, labels) {
	return new Promise((resolve) => {
		if (confirmDone) confirmDone(false);
		$("confirm-title").textContent = title;
		$("confirm-text").textContent = text;
		const yes = $("confirm-yes");
		const no = $("confirm-no");
		yes.textContent = labels?.yes || t("confirm.yes");
		no.textContent = labels?.no || t("confirm.no");
		$("confirm-modal").classList.remove("hidden");
		const done = (ok) => {
			if (!confirmDone) return;
			confirmDone = null;
			$("confirm-modal").classList.add("hidden");
			yes.onclick = null;
			no.onclick = null;
			yes.textContent = t("confirm.yes");
			no.textContent = t("confirm.no");
			resolve(!!ok);
		};
		confirmDone = done;
		yes.onclick = () => done(true);
		no.onclick = () => done(false);
	});
}

function hostView(h) {
	const revealed = revealedAddrSet();
	const state = wakingAddrs.has(h.addr) ? "waking" : (h.state || "unknown");
	const registered = h.registered ? t("host.registered") : t("host.unregistered");
	const origin = h.discovered && h.manual ? t("host.discoveredManual")
		: h.discovered ? t("host.discovered") : t("host.manual");
	const idLine = t("host.id", { id: h.id || "—", status: registered });
	const actionLabel = h.manual && !h.discovered ? t("host.delete") : (!h.registered ? t("host.hide") : "");
	const act = h.manual && !h.discovered ? "delete" : "hide";
	const stateKey = "state." + state;
	const stateName = t(stateKey) === stateKey ? state : t(stateKey);
	const stateText = t("host.state", { state: stateName }) + (h.appName ? "\n" + t("host.app", { app: h.appName }) : "");
	const addrShown = revealed.has(h.addr);
	const displayAddr = addrShown ? h.addr : maskHostAddr(h.addr);
	const rawName = h.name || h.addr;
	const displayName = (!h.name || h.name === h.addr) && !addrShown ? maskHostAddr(h.addr) : rawName;
	const iconCls = state === "ready" ? "ready" : (state === "standby" || state === "waking" ? "standby" : "unknown");
	const wantWake = !!(h.registered && h.registKey && state !== "ready");
	return { h, state, origin, idLine, actionLabel, act, stateText, addrShown, displayAddr, displayName, iconCls, wantWake };
}

function hostStructureSig(views) {
	return uiLanguage() + "\n" + views.map((v) => [
		v.h.addr, v.h.name || "", Number(!!v.h.ps5), Number(!!v.h.registered),
		Number(!!v.h.discovered), Number(!!v.h.manual), Number(!!v.addrShown),
		v.act, v.actionLabel, v.h.id || ""
	].join("\t")).join("\n");
}

function syncWakeButton(card, v) {
	const actions = card.querySelector(".actions");
	if (!actions) return;
	let wake = actions.querySelector("[data-act='wake']");
	if (v.wantWake && !wake) {
		wake = document.createElement("button");
		wake.type = "button";
		wake.className = "ghost";
		wake.dataset.act = "wake";
		wake.innerHTML = `<span class="box-icon"></span>${t("host.wake")}`;
		const host = v.h;
		wake.onclick = async (ev) => {
			ev.stopPropagation();
			await ensureWasmRuntime();
			ensureDiscovery();
			if (api.wakeup) api.wakeup(host.addr, host.registKey, host.ps5 ? 1 : 0);
			log(t("log.waking"));
		};
		actions.appendChild(wake);
	} else if (!v.wantWake && wake) {
		wake.remove();
	}
}

function patchHostCards(list, views) {
	const cards = [...list.querySelectorAll(".host-card")];
	if (cards.length !== views.length) return false;
	for (let i = 0; i < views.length; i++) {
		if (cards[i].getAttribute("data-addr") !== views[i].h.addr) return false;
	}
	for (let i = 0; i < views.length; i++) {
		const card = cards[i];
		const v = views[i];
		card.classList.toggle("selected", selectedAddr === v.h.addr);
		const icon = card.querySelector(".icon");
		if (icon) {
			icon.classList.remove("ready", "standby", "unknown");
			icon.classList.add(v.iconCls);
		}
		const stateEl = card.querySelector(".state");
		if (stateEl) stateEl.textContent = v.stateText;
		syncWakeButton(card, v);
	}
	return true;
}

function bindHostCard(card, h, v) {
	const selectCard = () => {
		selectedAddr = h.addr;
		document.querySelectorAll(".host-card").forEach((c) => c.classList.toggle("selected", c === card));
	};
	const connect = () => {
		selectCard();
		if (connecting || streaming) return;
		if (h.registered) startStream(h);
		else openRegist(h);
	};
	card.onclick = (ev) => {
		if (ev.target.closest("[data-act], .host-name-input")) return;
		connect();
	};
	card.onkeydown = (ev) => {
		if (ev.target.classList.contains("host-name-input")) return;
		if (ev.key === "Enter" || ev.key === " ") { ev.preventDefault(); connect(); }
	};
	card.querySelectorAll("[data-act=\"rename\"]").forEach((el) => {
		el.onclick = (ev) => {
			ev.stopPropagation();
			selectCard();
			beginHostRename(card, h);
		};
	});
	const revealBtn = card.querySelector("[data-act=\"reveal\"]");
	if (revealBtn) {
		revealBtn.onclick = (ev) => {
			ev.stopPropagation();
			toggleRevealAddr(h.addr);
			lastHostListSig = "";
			renderHosts();
		};
	}
	const actBtn = card.querySelector("[data-act=\"hide\"], [data-act=\"delete\"]");
	if (actBtn) {
		actBtn.onclick = async (ev) => {
			ev.stopPropagation();
			selectCard();
			if (actBtn.dataset.act === "hide") {
				const ok = await askConfirm(t("confirm.hideTitle"), t("confirm.hideText"));
				if (!ok) return;
				const set = hiddenSet();
				set.add(h.addr);
				if (normAddr(h.addr)) set.add(normAddr(h.addr));
				persistHidden(set);
				lastHostListSig = "";
				renderHosts();
				return;
			}
			const ok = await askConfirm(t("confirm.deleteTitle"), t("confirm.deleteText"));
			if (!ok) return;
			persistHosts(savedHosts().filter((x) => x.host !== h.addr && normAddr(x.host) !== normAddr(h.addr)));
		};
	}
	syncWakeButton(card, v);
}

function renderHosts() {
	const list = $("hosts");
	if (!list) return;
	if (hostsPointerDown || list.querySelector(".host-name-input")) return;
	const views = mergedHosts().map(hostView);
	const sig = hostStructureSig(views);
	if (sig === lastHostListSig && list.dataset.ready === "1" && patchHostCards(list, views))
		return;
	lastHostListSig = sig;
	list.dataset.ready = "1";
	list.innerHTML = "";
	for (const v of views) {
		const h = v.h;
		const li = document.createElement("li");
		li.innerHTML = `
			<div class="host-card ${selectedAddr === h.addr ? "selected" : ""}" data-addr="${escapeHtml(h.addr)}" role="button" tabindex="0">
				${iconHtml(!!h.ps5, v.state, h.addr)}
				<div class="meta">
					<div class="host-name-row">
						<div class="host-name" data-act="rename">${escapeHtml(v.displayName)}</div>
						<button type="button" class="host-chip host-chip-icon" data-act="rename" title="${escapeHtml(t("host.rename"))}" aria-label="${escapeHtml(t("host.rename"))}">
							<svg viewBox="0 0 24 24" width="16" height="16" aria-hidden="true"><path fill="currentColor" d="M3 17.25V21h3.75L17.81 9.94l-3.75-3.75L3 17.25zm17.71-10.21a1 1 0 0 0 0-1.41l-2.34-2.34a1 1 0 0 0-1.41 0l-1.83 1.83 3.75 3.75 1.83-1.83z"/></svg>
						</button>
					</div>
					<div class="host-addr-row">
						<span>${escapeHtml(t("host.address", { addr: v.displayAddr }))}</span>
						<button type="button" class="host-chip" data-act="reveal" aria-pressed="${v.addrShown ? "true" : "false"}">${escapeHtml(v.addrShown ? t("host.hideAddr") : t("host.showAddr"))}</button>
					</div>
					<div>${escapeHtml(v.idLine)}</div>
					<div>${escapeHtml(v.origin)}</div>
				</div>
				<div class="state">${escapeHtml(v.stateText)}</div>
				<div class="actions">${v.actionLabel ? `<button type="button" class="ghost" data-act="${v.act}"><span class="box-icon"></span>${v.actionLabel}</button>` : ""}</div>
			</div>`;
		bindHostCard(li.querySelector(".host-card"), h, v);
		list.appendChild(li);
	}
}

function savedListEditingPsn() {
	const el = $("saved-list");
	const active = document.activeElement;
	return !!(el && active && el.contains(active) && active.matches("[data-act='psn']"));
}

function consoleAdminSignature(rows) {
	return uiLanguage() + "\n" + rows.map((r) =>
		[r.addr, r.name, Number(!!r.ps5), Number(!!r.saved), Number(!!r.hidden), r.psnId || "", Number(!!r.registered)].join("\t")
	).join("\n");
}

function commitConsolePsn(input, row) {
	if (!input || !row) return;
	const psnId = normalizePsnAccountId(input.value);
	input.value = psnId;
	upsertHostPsn(row.addr, psnId, row);
}

function renderSavedList() {
	const el = $("saved-list");
	if (!el) return;
	if (savedListEditingPsn()) return;
	const rows = consoleAdminRows();
	const sig = consoleAdminSignature(rows);
	if (sig === lastConsoleAdminSig && el.dataset.ready === "1") return;
	lastConsoleAdminSig = sig;
	el.dataset.ready = "1";
	el.innerHTML = "";
	if (!rows.length) {
		const li = document.createElement("li");
		li.className = "console-admin-empty";
		li.textContent = t("consoles.empty");
		el.appendChild(li);
		return;
	}
	for (const row of rows) {
		const li = document.createElement("li");
		li.dataset.addr = row.addr;
		const kind = row.ps5 ? "PS5" : "PS4";
		const bits = [kind];
		if (row.registered) bits.push(t("host.registered"));
		if (!row.saved) bits.push(t("consoles.discovered"));
		if (row.hidden) bits.push(t("consoles.hiddenBadge"));
		const title = row.name && row.name !== row.addr ? row.name : maskHostAddr(row.addr);
		li.innerHTML =
			`<div class="console-admin-main">` +
				`<div class="console-admin-meta">` +
					`<strong>${escapeHtml(title)}</strong>` +
					`<div>${escapeHtml(maskHostAddr(row.addr))} · ${escapeHtml(bits.join(" · "))}</div>` +
				`</div>` +
				`<div class="console-admin-actions">` +
					`<button type="button" class="ghost" data-act="${row.hidden ? "show" : "hide"}">${escapeHtml(t(row.hidden ? "consoles.show" : "consoles.hide"))}</button>` +
					(row.saved ? `<button type="button" class="ghost" data-act="delete">${escapeHtml(t("consoles.delete"))}</button>` : "") +
				`</div>` +
			`</div>` +
			`<div class="console-admin-psn">` +
				`<label>${escapeHtml(t("consoles.psn"))}</label>` +
				`<input type="text" data-act="psn" name="psn-${escapeHtml(row.addr)}" data-i18n-placeholder="consoles.psnPh" placeholder="${escapeHtml(t("consoles.psnPh"))}" value="${escapeHtml(row.psnId || "")}" autocomplete="off" spellcheck="false">` +
			`</div>`;
		const hideBtn = li.querySelector("[data-act='hide'], [data-act='show']");
		if (hideBtn) {
			hideBtn.onclick = async () => {
				const set = hiddenSet();
				if (row.hidden) {
					set.delete(row.addr);
					set.delete(normAddr(row.addr));
				} else {
					const ok = await askConfirm(t("confirm.hideTitle"), t("confirm.hideText"));
					if (!ok) return;
					set.add(row.addr);
					if (normAddr(row.addr)) set.add(normAddr(row.addr));
				}
				persistHidden(set);
			};
		}
		const delBtn = li.querySelector("[data-act='delete']");
		if (delBtn) {
			delBtn.onclick = async () => {
				const ok = await askConfirm(t("confirm.deleteTitle"), t("confirm.deleteText"));
				if (!ok) return;
				persistHosts(savedHosts().filter((x) => x.host !== row.addr && normAddr(x.host) !== normAddr(row.addr)));
			};
		}
		const psnInput = li.querySelector("[data-act='psn']");
		if (psnInput) {
			psnInput.addEventListener("keydown", (ev) => ev.stopPropagation());
			psnInput.addEventListener("keyup", (ev) => ev.stopPropagation());
			psnInput.addEventListener("blur", () => {
				commitConsolePsn(psnInput, row);
				lastConsoleAdminSig = "";
				renderSavedList();
			});
		}
		el.appendChild(li);
	}
}

function consoleAdminRows() {
	const hidden = hiddenSet();
	const seen = new Set();
	const rows = [];
	const isHid = (addr) => hidden.has(addr) || hidden.has(normAddr(addr));
	for (const h of savedHosts()) {
		const addr = h.host;
		seen.add(normAddr(addr));
		rows.push({
			addr,
			name: h.name || addr,
			ps5: !!h.ps5,
			saved: true,
			hidden: isHid(addr),
			psnId: h.psnId || "",
			registered: !!(h.registKey && h.morning)
		});
	}
	if (discoveryOn || cloud.homeProxy) {
		for (const h of discovered) {
			const ip = normAddr(h.addr);
			if (!ip || seen.has(ip)) continue;
			seen.add(ip);
			rows.push({
				addr: h.addr,
				name: h.name || h.addr,
				ps5: !!h.ps5,
				saved: false,
				hidden: isHid(h.addr),
				psnId: "",
				registered: false
			});
		}
	}
	return rows;
}

function upsertHostPsn(addr, psnId, meta) {
	const list = savedHosts();
	const existing = list.find((h) => h.host === addr || normAddr(h.host) === normAddr(addr));
	if (existing) {
		if ((existing.psnId || "") === psnId) return;
		existing.psnId = psnId;
		persistHosts(list);
		return;
	}
	rememberHost({
		host: addr,
		name: (meta && meta.name) || addr,
		ps5: !!(meta && meta.ps5),
		id: "",
		registKey: "",
		morning: "",
		psnId
	});
}

function renameHost(addr, name) {
	const trimmed = (name || "").trim();
	if (!trimmed) {
		renderHosts();
		return;
	}
	const saved = savedHosts();
	const existing = saved.find((h) => h.host === addr);
	if (existing) {
		if (existing.name === trimmed) {
			renderHosts();
			return;
		}
		existing.name = trimmed;
		persistHosts(saved);
		return;
	}
	const disc = discovered.find((d) => d.addr === addr) || {};
	rememberHost({
		host: addr,
		name: trimmed,
		ps5: !!disc.ps5,
		id: disc.id || "",
		registKey: "",
		morning: ""
	});
}

function beginHostRename(card, host) {
	const nameEl = card.querySelector(".host-name");
	if (!nameEl || card.querySelector(".host-name-input")) return;
	const input = document.createElement("input");
	input.type = "text";
	input.className = "host-name-input";
	input.value = host.name || host.addr || "";
	input.maxLength = 64;
	input.setAttribute("aria-label", t("host.rename"));
	input.autocomplete = "off";
	nameEl.replaceWith(input);
	input.focus();
	input.select();
	const finish = (save) => {
		if (input.dataset.done) return;
		input.dataset.done = "1";
		if (save) renameHost(host.addr, input.value);
		else renderHosts();
	};
	input.onkeydown = (ev) => {
		ev.stopPropagation();
		if (ev.key === "Enter") {
			ev.preventDefault();
			input.blur();
		} else if (ev.key === "Escape") {
			ev.preventDefault();
			finish(false);
		}
	};
	input.onmousedown = (ev) => ev.stopPropagation();
	input.onclick = (ev) => ev.stopPropagation();
	input.onblur = () => finish(true);
}

function findSavedHost(addr) {
	const ip = normAddr(addr);
	return savedHosts().find((h) => h.host === addr || (ip && normAddr(h.host) === ip)) || null;
}

function psnIdForHost(host) {
	if (host && host.psnId) return host.psnId;
	const saved = findSavedHost(host && (host.addr || host.host));
	if (saved && saved.psnId) return saved.psnId;
	return settings.psnId || "";
}

function rememberHost(info) {
	const list = savedHosts();
	const ip = normAddr(info.host);
	const prev = list.find((h) => h.host === info.host || (ip && normAddr(h.host) === ip));
	const merged = {
		host: prev?.host || info.host,
		name: info.name || prev?.name || info.host,
		ps5: info.ps5 != null ? !!info.ps5 : !!prev?.ps5,
		id: info.id || prev?.id || "",
		registKey: info.registKey != null && info.registKey !== "" ? info.registKey : (prev?.registKey || ""),
		morning: info.morning != null && info.morning !== "" ? info.morning : (prev?.morning || ""),
		psnId: info.psnId != null ? String(info.psnId) : (prev?.psnId || "")
	};
	persistHosts([merged, ...list.filter((h) => h.host !== merged.host && normAddr(h.host) !== normAddr(merged.host))]);
}

function looksLikeIpv6(host) {
	const t = (host || "").trim();
	if (!t) return false;
	if (/^\d{1,3}(\.\d{1,3}){3}(:\d+)?$/.test(t)) return false;
	return t.includes(":");
}

function isPrivateIpv4(host) {
	const m = (host || "").trim().match(/^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})(?::\d+)?$/);
	if (!m) return false;
	const a = Number(m[1]);
	const b = Number(m[2]);
	if (a === 10 || a === 127) return true;
	if (a === 192 && b === 168) return true;
	if (a === 172 && b >= 16 && b <= 31) return true;
	return false;
}

function openSettingsTab(name) {
	showView("settings");
	$("settings-tabs").querySelectorAll("button").forEach((b) => b.classList.toggle("active", b.dataset.tab === name));
	document.querySelectorAll(".tab-page").forEach((p) => p.classList.toggle("hidden", p.dataset.page !== name));
}

async function showView(name, opts) {
	const prev = currentView;
	currentView = name;
	if (name !== "stream") await exitStreamFullscreen();
	$("welcome").classList.toggle("hidden", name !== "welcome");
	$("welcome-bar").classList.toggle("hidden", name !== "welcome");
	$("settings-view").classList.toggle("hidden", name !== "settings");
	$("stream-view").classList.toggle("hidden", name !== "stream");
	syncDocumentTitle();
	if (name === "stream") {
		if (opts?.fullscreen !== false) enterStreamFullscreen();
		syncStreamChrome();
	}
	syncPointerLock();
	if (name === "stream") tryPointerLock();
	if (name === "welcome" && prev !== "welcome") {
		scheduleRenderHosts();
		restartDiscovery();
	}
}

function isFullscreen() {
	return !!(document.fullscreenElement || document.webkitFullscreenElement);
}

function isHandheld() {
	const ua = navigator.userAgent || "";
	if (/Android|iPhone|iPod|Mobile/i.test(ua)) return true;
	if (/iPad|Tablet|Silk/i.test(ua)) return true;
	if (navigator.platform === "MacIntel" && navigator.maxTouchPoints > 1) return true;
	return navigator.maxTouchPoints > 0
		&& matchMedia("(hover: none) and (pointer: coarse)").matches
		&& Math.min(screen.width, screen.height) <= 1200;
}

function syncHandheldChrome() {
	document.documentElement.classList.toggle("handheld", isHandheld());
}

function viewportSize() {
	const fs = document.fullscreenElement || document.webkitFullscreenElement;
	if (fs && fs.clientWidth && fs.clientHeight)
		return { w: fs.clientWidth, h: fs.clientHeight };
	const vv = window.visualViewport;
	if (vv && vv.width && vv.height)
		return { w: vv.width, h: vv.height };
	return { w: window.innerWidth, h: window.innerHeight };
}

function applyViewportVars() {
	const { w, h } = viewportSize();
	const root = document.documentElement;
	root.style.setProperty("--vvw", Math.round(w) + "px");
	root.style.setProperty("--vvh", Math.round(h) + "px");
}

function osLandscape() {
	const type = (screen.orientation && screen.orientation.type) || "";
	if (type) return type.startsWith("landscape");
	const { w, h } = viewportSize();
	return w > h;
}

async function lockLandscape() {
	if (!isHandheld() || !isFullscreen()) return;
	try {
		if (screen.orientation && screen.orientation.lock)
			await screen.orientation.lock("landscape");
	} catch {
		try {
			if (screen.orientation && screen.orientation.lock)
				await screen.orientation.lock("landscape-primary");
		} catch {}
	}
}

function unlockOrientation() {
	try {
		if (screen.orientation && screen.orientation.unlock)
			screen.orientation.unlock();
	} catch {}
}

function syncForcedLandscape() {
	const stream = $("stream-view");
	if (!stream) return;
	const { w, h } = viewportSize();
	const need = isFullscreen()
		&& !stream.classList.contains("hidden")
		&& isHandheld()
		&& !osLandscape()
		&& h > w + 40;
	stream.classList.toggle("fs-landscape", need);
}

function syncFullscreenButton() {
	const btn = $("btn-stream-fs");
	const img = $("btn-stream-fs-icon");
	if (!btn || !img) return;
	const on = isFullscreen();
	const key = on ? "stream.exitFullscreen" : "stream.fullscreen";
	btn.setAttribute("aria-pressed", on ? "true" : "false");
	btn.dataset.i18nTitle = key;
	btn.title = t(key);
	img.src = on ? "icons/fullscreen-exit-24px.svg" : "icons/fullscreen-24px.svg";
}

function syncStopButton() {
	const btn = $("btn-stop");
	if (!btn) return;
	const key = isFullscreen() ? "stream.exitFullscreen" : "stream.stop";
	btn.dataset.i18nTitle = key;
	btn.title = t(key);
}

function videoObjectFit() {
	const mode = String(settings.window || "0");
	if (mode === "5") return "fill";
	if (mode === "4") return "cover";
	return "contain";
}

function fitVideoToStage() {
	if (!canvas) return;
	const stage = canvas.parentElement;
	if (!stage) return;
	const sw = stage.clientWidth;
	const sh = stage.clientHeight;
	const fit = videoObjectFit();
	if (sw < 8 || sh < 8 || fit === "fill" || fit === "cover") {
		canvas.style.width = "100%";
		canvas.style.height = "100%";
		return;
	}
	const vw = canvas.width || 16;
	const vh = canvas.height || 9;
	const scale = Math.min(sw / vw, sh / vh);
	const w = Math.max(1, Math.round(vw * scale));
	const h = Math.max(1, Math.round(vh * scale));
	canvas.style.width = w + "px";
	canvas.style.height = h + "px";
}

function syncStreamChrome() {
	const stream = $("stream-view");
	if (!stream) return;
	const playing = !stream.classList.contains("hidden");
	const fs = isFullscreen() && playing;
	stream.classList.toggle("is-fullscreen", fs);
	document.documentElement.classList.toggle("chiaki-fs", fs);
	applyViewportVars();
	if (!playing) stream.classList.remove("show-chrome");
	if (!fs) unlockOrientation();
	else lockLandscape();
	syncForcedLandscape();
	syncFullscreenButton();
	syncStopButton();
	const fit = videoObjectFit();
	stream.classList.toggle("fit-cover", fit === "cover");
	stream.classList.toggle("fit-fill", fit === "fill");
	if (canvas) canvas.style.objectFit = fit;
	fitVideoToStage();
	requestAnimationFrame(fitVideoToStage);
}

let streamChromeTimer = 0;
function hideStreamChrome() {
	const stream = $("stream-view");
	if (!stream) return;
	const bar = stream.querySelector(".stream-bar");
	const active = document.activeElement;
	if (bar && active && bar.contains(active) && typeof active.blur === "function")
		active.blur();
	stream.classList.remove("show-chrome");
}
function onStreamChromeMove(ev) {
	const stream = $("stream-view");
	if (!stream || stream.classList.contains("hidden")) return;
	const overBar = !!(ev.target && ev.target.closest && ev.target.closest(".stream-bar"));
	const nearTop = ev.clientY <= 80 || overBar;
	clearTimeout(streamChromeTimer);
	if (nearTop) stream.classList.add("show-chrome");
	else streamChromeTimer = setTimeout(hideStreamChrome, 400);
}

async function enterStreamFullscreen() {
	const stream = $("stream-view");
	if (!stream || stream.classList.contains("hidden")) return;
	if (isFullscreen()) {
		syncStreamChrome();
		return;
	}
	const tryFs = async (el, opts) => {
		if (el.requestFullscreen) await el.requestFullscreen(opts || { navigationUI: "hide" });
		else if (el.webkitRequestFullscreen) el.webkitRequestFullscreen();
	};
	try {
		await tryFs(stream);
	} catch {
		try { await tryFs(document.documentElement); }
		catch {
			try { await tryFs(stream, {}); }
			catch {}
		}
	}
	await lockLandscape();
	syncStreamChrome();
	setTimeout(syncStreamChrome, 80);
	setTimeout(syncStreamChrome, 280);
	setTimeout(syncStreamChrome, 700);
}

function waitForFullscreenExit(timeoutMs) {
	if (!isFullscreen()) return Promise.resolve();
	return new Promise((resolve) => {
		let done = false;
		const finish = () => {
			if (done) return;
			done = true;
			document.removeEventListener("fullscreenchange", onChange);
			document.removeEventListener("webkitfullscreenchange", onChange);
			resolve();
		};
		const onChange = () => { if (!isFullscreen()) finish(); };
		document.addEventListener("fullscreenchange", onChange);
		document.addEventListener("webkitfullscreenchange", onChange);
		setTimeout(finish, timeoutMs || 1200);
	});
}

async function exitStreamFullscreen() {
	if (!isFullscreen()) {
		syncStreamChrome();
		return;
	}
	unlockOrientation();
	try {
		if (document.exitFullscreen) await document.exitFullscreen();
		else if (document.webkitExitFullscreen) document.webkitExitFullscreen();
	} catch {}
	await waitForFullscreenExit(1200);
	syncStreamChrome();
}

function setStreamOverlay(text, kind) {
	const el = $("overlay");
	if (!el) return;
	if (!text) {
		el.classList.add("hidden");
		el.textContent = "";
		el.classList.remove("error", "paused");
		return;
	}
	el.textContent = text;
	el.classList.toggle("error", kind === "error");
	el.classList.toggle("paused", kind === "paused");
	el.classList.remove("hidden");
}

function openSessionGate() {
	closeSessionGate({ type: "superseded" });
	let resolve;
	const promise = new Promise((r) => { resolve = r; });
	sessionGate = { resolve, promise };
	return promise;
}

function closeSessionGate(result) {
	if (!sessionGate) return;
	const gate = sessionGate;
	sessionGate = null;
	gate.resolve(result);
}

function isBusyQuit(code) {
	const n = Number(code);
	return n === 1 || n === 2 || n === 3 || n === 4;
}

function shouldSuppressConnectLog(msg) {
	if (!retryingConnect) return false;
	return /0x80108b10|already in use|Session has quit|Session request connect failed/i.test(String(msg || ""));
}

function discoveryEnabled() {
	if (settings.discoveryEnabled === false) return false;
	if (cloud.homeProxy) return true;
	return cloud.discoveryEnabled !== false;
}

function applyDiscoveryUi() {
	const serverOn = cloud.discoveryEnabled !== false;
	const allowed = discoveryEnabled();
	const btn = $("btn-discover");
	if (btn) {
		btn.classList.toggle("hidden", !allowed);
		btn.disabled = !allowed;
	}
	$("s-discovery-row")?.classList.toggle("hidden", !serverOn);
	if (!allowed && discoveryOn) {
		discoveryOn = false;
		const fab = $("fab-icon");
		if (btn) btn.setAttribute("aria-pressed", "false");
		if (fab) fab.src = "icons/discover-off-24px.svg";
	}
	syncDiscoveryService();
}

function probeHostList() {
	return savedHosts()
		.map((h) => String(h.host || "").trim())
		.filter((host) => host && !looksLikeIpv6(host));
}

function syncDiscoveryService() {
	if (discoveryPaused) return;
	if (!api || typeof api.discoverStart !== "function") return;
	const extras = probeHostList().join(",");
	const wantLan = discoveryEnabled() && discoveryOn;
	const want = !!(extras || wantLan);
	const key = `${wantLan ? "lan" : "probe"}|${extras}`;
	if (!want) {
		if (probeRunning) {
			try { api.discoverStop?.(); } catch {}
			probeRunning = false;
			lastProbeKey = "";
		}
		return;
	}
	if (probeRunning && key === lastProbeKey) return;
	lastProbeKey = key;
	probeRunning = true;
	try { api.discoverStart(extras); } catch {}
}

function stopDiscovery() {
	discoveryOn = false;
	const btn = $("btn-discover");
	if (btn) btn.setAttribute("aria-pressed", "false");
	const fab = $("fab-icon");
	if (fab) fab.src = "icons/discover-off-24px.svg";
	syncDiscoveryService();
}

async function startDiscovery() {
	if (!discoveryEnabled()) {
		syncDiscoveryService();
		return;
	}
	if (typeof api.discoverStart !== "function") {
		const ok = await ensureWasmRuntime();
		if (!ok) return;
	}
	discoveryOn = true;
	const btn = $("btn-discover");
	if (btn) btn.setAttribute("aria-pressed", "true");
	const fab = $("fab-icon");
	if (fab) fab.src = "icons/discover-24px.svg";
	syncDiscoveryService();
}

function ensureDiscovery() {
	syncDiscoveryService();
}

function pauseDiscoveryForStream(pause) {
	discoveryPaused = !!pause;
	if (pause) {
		if (probeRunning) {
			try { api.discoverStop?.(); } catch {}
			probeRunning = false;
			lastProbeKey = "";
		}
		return;
	}
	syncDiscoveryService();
}

async function wakeAndWait(host, seq) {
	if (!host.registKey) return false;
	ensureDiscovery();
	wakingAddrs.add(host.addr);
	renderHosts();
	log(t("log.waking"));
	const sendWake = () => {
		if (api.wakeup) api.wakeup(host.addr, host.registKey, host.ps5 ? 1 : 0);
	};
	sendWake();
	const deadline = Date.now() + 35000;
	let lastWake = Date.now();
	while (Date.now() < deadline) {
		if (seq !== connectSeq) return false;
		const live = findDiscoveryRow(host.addr, host.id);
		if ((live?.state || "") === "ready") {
			wakingAddrs.delete(host.addr);
			renderHosts();
			log(t("log.wakeReady"));
			return true;
		}
		if (Date.now() - lastWake > 4000) {
			sendWake();
			lastWake = Date.now();
		}
		await sleep(400);
	}
	wakingAddrs.delete(host.addr);
	renderHosts();
	log(t("log.wakeTimeout"), 0);
	return false;
}

async function failConnect(seq, message) {
	retryingConnect = false;
	ignoreQuit = false;
		streaming = false;
		setStreamOverlay(message, "error");
		log(message, 0);
		shareOnStreaming(false);
	await sleep(4000);
	if (seq !== connectSeq) return;
	activeHost = null;
	streamTitleName = "";
	appliedStreamKey = "";
	setStreamOverlay("");
	showView("welcome");
	pauseDiscoveryForStream(false);
	renderHosts();
}

async function startStream(host) {
	if (connecting || streaming) return;
	connecting = true;
	activeHost = host;
	streamTitleName = (host.name && host.name !== host.addr) ? host.name : t("title.stream");
	syncDocumentTitle();
	const seq = ++connectSeq;
	try {
		saveSettings();
		const wasmUp = wasmRuntimeReady();
		showView("stream", { fullscreen: wasmUp });
		setStreamOverlay(wasmUp ? t("log.connecting") : t("log.wasmLoading"));
		const wasmOk = await ensureWasmRuntime();
		if (seq !== connectSeq) return;
		if (!wasmOk || typeof api.sessionStart !== "function") {
			log(t("log.wasmFailed"), 0);
			setStreamOverlay(t("log.wasmFailed"), "error");
			await sleep(2500);
			if (seq !== connectSeq) return;
			activeHost = null;
			streamTitleName = "";
			appliedStreamKey = "";
			setStreamOverlay("");
			showView("welcome");
			pauseDiscoveryForStream(false);
			renderHosts();
			return;
		}
		if (!isFullscreen()) enterStreamFullscreen();
		resetVideoDecoder();
		codec = await resolveStreamCodec(Number(settings.codec));
		if (audio.ctx) {
			try { await audio.ctx.resume(); } catch {}
			resetAudioOut();
		}
		syncDiscoveryService();
		let live = findDiscoveryRow(host.addr, host.id);
		let state = live?.state || host.state || "unknown";
		if (state === "unknown" && host.registKey) {
			const waitUntil = Date.now() + 2500;
			while (Date.now() < waitUntil && seq === connectSeq) {
				live = findDiscoveryRow(host.addr, host.id);
				if ((live?.state || "") === "ready" || (live?.state || "") === "standby") {
					state = live.state;
					break;
				}
				await sleep(200);
			}
		}
		if (state !== "ready" && host.registKey) {
			await wakeAndWait(host, seq);
			if (seq !== connectSeq) return;
		}
		if (seq !== connectSeq) return;
		settingsReturnView = "stream";
		syncVpadUi();
		lastPadSent = "";
		pauseDiscoveryForStream(true);
		showView("stream");
		setStreamOverlay(t("log.connecting"));
		retryingConnect = true;
		const ps5 = host.ps5 ? 1 : Number(settings.console);
		const deadline = Date.now() + 20000;
		let attempt = 0;
		while (Date.now() < deadline) {
			if (seq !== connectSeq) return;
			if (attempt > 0) {
				ignoreQuit = true;
				if (api.sessionStop) api.sessionStop();
				await sleep(2000);
				ignoreQuit = false;
			}
			if (seq !== connectSeq) return;
			setStreamOverlay(t("log.connecting"));
			const outcomeP = openSessionGate();
			const kbps = effectiveBitrate();
			if (kbps !== Number(settings.bitrate))
				log(t("log.bitrateCapped", { kbps: String(kbps) }));
			const r = api.sessionStart(
				host.addr,
				host.registKey,
				host.morning,
				ps5,
				Number(settings.resolution),
				Number(settings.fps),
				codec,
				kbps
			);
			if (r !== 0) {
				closeSessionGate({ type: "fail" });
				attempt++;
				continue;
			}
			streaming = true;
			const outcome = await Promise.race([
				outcomeP,
				sleep(10000).then(() => ({ type: "timeout" }))
			]);
			if (seq !== connectSeq) return;
			if (outcome.type === "connected") {
				retryingConnect = false;
				ignoreQuit = false;
				appliedStreamKey = streamConfigKey();
				setStreamOverlay("");
				return;
			}
			streaming = false;
			if (outcome.type === "quit" && !isBusyQuit(outcome.code)) {
				await failConnect(seq, quitLabel(outcome.code, outcome.detail));
				return;
			}
			attempt++;
		}
		await failConnect(seq, t("log.connectFailed"));
	} finally {
		retryingConnect = false;
		if (seq === connectSeq) {
			connecting = false;
			wakingAddrs.delete(host.addr);
		}
	}
}

async function restartActiveStream() {
	const host = activeHost;
	if (!host) return;
	ignoreQuit = true;
	connectSeq++;
	streaming = false;
	vpadPtrs.clear();
	vpadHeld.clear();
	setStreamOverlay(t("log.connecting"));
	if (api.sessionStop) api.sessionStop();
	await sleep(400);
	connecting = false;
	streaming = false;
	await startStream(host);
}

function sessionIsConnected() {
	return streaming && !connecting && !retryingConnect;
}

async function putConsoleToSleep() {
	if (!api.sessionGotoBed) return false;
	const r = api.sessionGotoBed();
	if (r !== 0) return false;
	log(t("log.goingToSleep"));
	await sleep(450);
	return true;
}

async function requestDisconnect() {
	if (sessionStopping) return;
	const connected = sessionIsConnected();
	let sleepConsole = false;
	if (connected) {
		const action = String(settings.disconnect || "2");
		if (action === "1") sleepConsole = true;
		else if (action === "2") {
			sleepConsole = await askConfirm(
				t("disconnect.title"),
				t("disconnect.text"),
				{ yes: t("disconnect.sleep"), no: t("disconnect.no") }
			);
		}
	}
	await stopSession({ sleep: sleepConsole });
}

function onAppSuspend() {
	if (sessionStopping || !sessionIsConnected()) return;
	if (String(settings.suspend) !== "1") return;
	stopSession({ sleep: true });
}

async function stopSession(opts = {}) {
	if (sessionStopping) return;
	sessionStopping = true;
	try {
		if (opts.sleep && sessionIsConnected()) {
			ignoreQuit = true;
			await putConsoleToSleep();
		} else {
			ignoreQuit = true;
		}
		connectSeq++;
		connecting = false;
		retryingConnect = false;
		wakingAddrs.clear();
		streaming = false;
		activeHost = null;
		streamTitleName = "";
		appliedStreamKey = "";
		closeSessionGate({ type: "stopped" });
		vpadPtrs.clear();
		vpadHeld.clear();
		for (const timer of vpadReleaseTimers.values()) clearTimeout(timer);
		vpadReleaseTimers.clear();
		vpad = { buttons: 0, l2: 0, r2: 0, lx: 0, ly: 0, rx: 0, ry: 0 };
		settingsReturnView = "welcome";
		setStreamOverlay("");
		resetVideoDecoder();
		shareOnStreaming(false);
		syncPointerLock();
		if (api.sessionStop) api.sessionStop();
		await showView("welcome");
		renderHosts();
		ignoreQuit = false;
	} finally {
		sessionStopping = false;
	}
}

function quitLabel(code, detail) {
	const key = "quit." + code;
	const translated = t(key);
	if (translated !== key) return translated;
	return detail || String(code);
}

function numericToPsnB64(n) {
	const buf = new Uint8Array(8);
	let v = BigInt(n);
	for (let i = 0; i < 8; i++) {
		buf[i] = Number(v & 0xffn);
		v >>= 8n;
	}
	let bin = "";
	for (const b of buf) bin += String.fromCharCode(b);
	return btoa(bin);
}

function normalizePsnAccountId(raw) {
	const s = String(raw || "").trim().replace(/\s+/g, "");
	if (!s) return "";
	if (/^\d{5,20}$/.test(s)) return numericToPsnB64(s);
	return s;
}

function psnIdLooksValid(b64) {
	try {
		return atob(b64).length === 8;
	} catch {
		return false;
	}
}

function persistPsnId(b64) {
	if (!b64) return;
	$("s-psn-id").value = b64;
	if ($("reg-psn")) $("reg-psn").value = b64;
	saveSettings();
}

function lookupPsnUsername(username) {
	const name = String(username || "").trim();
	if (!name) {
		log(t("log.psnUserRequired"), 0);
		return "";
	}
	const url = "https://www.psntools.com/psn/checker/" + encodeURIComponent(name);
	const a = document.createElement("a");
	a.href = url;
	a.target = "_blank";
	a.rel = "noopener noreferrer";
	a.click();
	return "";
}

function openRegist(host) {
	$("reg-host").value = host.addr || "";
	$("reg-host").dataset.real = host.addr || "";
	$("reg-pin").value = "";
	$("reg-psn").value = psnIdForHost(host);
	$("reg-psn-user").value = "";
	$("regist-modal").classList.remove("hidden");
	$("regist-modal").dataset.ps5 = host.ps5 ? "1" : "0";
	$("regist-modal").dataset.name = host.name || "";
	$("psn-regist-fields").classList.toggle("hidden", !host.ps5);
}

function normalizeDiscovered(host) {
	return {
		addr: host.addr || "",
		name: host.name || host.addr || "",
		ps5: !!host.ps5,
		id: host.id || "",
		state: normalizeState(host.state),
		appName: host.appName || ""
	};
}

function rememberDiscoveredIds(rows) {
	try {
		const saved = savedHosts();
		let changed = false;
		for (const row of rows) {
			for (const h of saved) {
				if (!hostsMatchAddr(h, { addr: row.addr, host: h.host })) continue;
				if (row.id && h.id !== row.id) { h.id = row.id; changed = true; }
				if (row.name && h.name !== row.name && (!h.name || h.name === h.host)) {
					h.name = row.name;
					changed = true;
				}
			}
		}
		if (changed) {
			stateGen++;
			localStorage.setItem(HOSTS_KEY, JSON.stringify(saved));
			renderSavedList();
			scheduleCloudPush();
		}
	} catch {}
}

function discoveryAddrKey(h) {
	const addr = normAddr(h && (h.addr || h.host));
	return addr ? "addr:" + addr : "";
}

function applyHostsSnapshot(hosts) {
	const now = Date.now();
	const incoming = Array.isArray(hosts) ? hosts.map(normalizeDiscovered) : [];
	const graceMs = 15000;
	if (incoming.length > 0) {
		for (const h of incoming) {
			const k = discoveryAddrKey(h);
			if (!k) continue;
			const idx = discovered.findIndex((d) => hostsMatchAddr(d, h));
			if (idx >= 0) discovered[idx] = h;
			else discovered.unshift(h);
			discoveredSeenAt.set(k, now);
		}
	}
	discovered = discovered.filter((old) => {
		const k = discoveryAddrKey(old);
		const seen = k ? discoveredSeenAt.get(k) || 0 : 0;
		if (now - seen < graceMs) return true;
		if (k) discoveredSeenAt.delete(k);
		return false;
	});
	rememberDiscoveredIds(incoming);
	scheduleRenderHosts();
	if (currentView === "stream") syncDocumentTitle();
}

function addDiscovered(host) {
	const row = normalizeDiscovered(host);
	const k = discoveryAddrKey(row);
	const idx = discovered.findIndex((h) => hostsMatchAddr(h, row));
	if (idx >= 0) discovered[idx] = row;
	else discovered.unshift(row);
	if (k) discoveredSeenAt.set(k, Date.now());
	rememberDiscoveredIds([row]);
	scheduleRenderHosts();
}

function portStatusLabel(status) {
	if (status === "open") return t("add.portOpen");
	if (status === "closed") return t("add.portClosed");
	return t("add.portFiltered");
}

let addPortcheckOk = false;
let addPortcheckHost = "";

function syncAddOkButton() {
	const btn = $("add-ok");
	if (!btn) return;
	const host = ($("add-host")?.value || "").trim();
	const ready = addPortcheckOk && !!host && host === addPortcheckHost;
	btn.disabled = !ready;
	btn.title = ready ? "" : t("add.portcheckNeedTest");
}

function fillAddHostIpv4() {
	const input = $("add-host");
	if (!input) return;
	const ip = String(cloud.ipv4 || "").trim();
	if (cloud.homeProxy || cloud.homeProxyPending) return;
	if (ip && !input.value.trim()) input.value = ip;
}

function resetAddPortcheck() {
	addPortcheckOk = false;
	addPortcheckHost = "";
	syncAddOkButton();
}

async function testAddHostPorts() {
	const out = $("add-portcheck-out");
	const btn = $("add-portcheck");
	const host = ($("add-host")?.value || "").trim();
	if (!out || !btn) return;
	addPortcheckOk = false;
	addPortcheckHost = "";
	syncAddOkButton();
	if (!host) {
		out.classList.remove("hidden");
		out.innerHTML = `<p class="portcheck-sum bad">${escapeHtml(t("add.portcheckNeed"))}</p>`;
		return;
	}
	if (!/^(\d{1,3}\.){3}\d{1,3}$/.test(host) || looksLikeIpv6(host)) {
		out.classList.remove("hidden");
		out.innerHTML = `<p class="portcheck-sum bad">${escapeHtml(t("add.portcheckBad"))}</p>`;
		return;
	}
	if (isPrivateIpv4(host) && !cloud.homeProxy) {
		out.classList.remove("hidden");
		const key = cloud.homeProxyPending ? "add.portcheckNeedApprove" : "add.portcheckNeedHome";
		out.innerHTML = `<p class="portcheck-sum bad">${escapeHtml(t(key))}</p>`;
		return;
	}
	btn.disabled = true;
	const prev = btn.textContent;
	btn.textContent = t("add.portcheckRun");
	out.classList.remove("hidden");
	out.innerHTML = `<p class="portcheck-sum">${escapeHtml(t("add.portcheckRun"))}</p>`;
	try {
		const { ok, body } = await cloudRequest("/api/portcheck", {
			method: "POST",
			body: JSON.stringify({ host }),
			timeoutMs: 12000
		});
		if (typeof body?.ipv4 === "string" && body.ipv4) cloud.ipv4 = body.ipv4;
		if (body.error === "need_home_proxy" || body.error === "home_proxy_pending") {
			out.innerHTML = `<p class="portcheck-sum bad">${escapeHtml(t(body.error === "home_proxy_pending" ? "add.portcheckNeedApprove" : "add.portcheckNeedHome"))}</p>`;
			return;
		}
		if (!ok || !Array.isArray(body.ports) || !body.ports.length) {
			out.innerHTML = `<p class="portcheck-sum bad">${escapeHtml(t(body.error === "invalid_host" ? "add.portcheckBad" : "add.portcheckFail"))}</p>`;
			return;
		}
		const tcp = body.ports.find((p) => p.port === 9295 && p.proto === "tcp");
		const tcpOk = !!(tcp && tcp.status === "open") || body.ok === true;
		const viaHome = body.via === "home" || cloud.homeProxy;
		const sumClass = tcpOk ? "ok" : "bad";
		const sum = tcpOk
			? t(viaHome ? "add.portcheckOkHome" : "add.portcheckOk")
			: t(viaHome ? "add.portcheckWarnHome" : "add.portcheckWarn");
		out.innerHTML = `<p class="portcheck-sum ${sumClass}">${escapeHtml(sum)}</p>`;
		if (tcpOk) {
			addPortcheckOk = true;
			addPortcheckHost = host;
		}
		syncAddOkButton();
	} catch {
		out.innerHTML = `<p class="portcheck-sum bad">${escapeHtml(t("add.portcheckFail"))}</p>`;
	} finally {
		btn.disabled = false;
		btn.textContent = prev || t("add.portcheck");
	}
}

function bindUi() {
	const hostList = $("hosts");
	if (hostList) {
		hostList.addEventListener("pointerdown", () => { hostsPointerDown = true; });
		const endHostPtr = () => {
			if (!hostsPointerDown) return;
			hostsPointerDown = false;
			scheduleRenderHosts();
		};
		window.addEventListener("pointerup", endHostPtr);
		window.addEventListener("pointercancel", endHostPtr);
	}
	$("btn-settings").onclick = () => {
		saveSettings();
		settingsReturnView = "welcome";
		showView("settings");
	};
	$("btn-stream-settings").onclick = () => {
		saveSettings();
		settingsReturnView = "stream";
		showView("settings");
	};
	$("btn-stream-fs").onclick = () => {
		if (isFullscreen()) exitStreamFullscreen();
		else enterStreamFullscreen();
	};
	$("btn-settings-back").onclick = async () => {
		await applySettingsNow();
		const backToStream = settingsReturnView === "stream" && streaming;
		await showView(backToStream ? "stream" : "welcome");
		if (streaming) {
			syncVpadUi();
			if (activeHost && streamConfigKey() !== appliedStreamKey)
				await restartActiveStream();
		}
	};
	$("btn-vpad").onclick = () => {
		if (share.isGuest) return;
		settings.vpadEnabled = !settings.vpadEnabled;
		if (!settings.vpadEnabled) vpad = { buttons: 0, l2: 0, r2: 0, lx: 0, ly: 0, rx: 0, ry: 0 };
		localStorage.setItem(SETTINGS_KEY, JSON.stringify(settings));
		syncVpadUi();
		scheduleCloudPush();
		shareBroadcastState();
	};
	$("btn-vpad-layout").onclick = openVpadEditor;
	$("btn-vpad-reset").onclick = resetVpadLayout;
	$("vpad-editor-done").onclick = closeVpadEditor;
	$("vpad-editor-reset").onclick = resetVpadLayout;
	$("btn-vpad-custom").onclick = () => $("vpad-custom-file")?.click();
	$("btn-vpad-custom-clear").onclick = clearVpadCustomImage;
	$("vpad-custom-file").onchange = (e) => {
		const file = e.target.files && e.target.files[0];
		e.target.value = "";
		if (file) setVpadCustomImage(file);
	};
	fillVpadThemePickers();
	fillShareVpadKeyPicker();
	$("share-vpad-keys-all")?.addEventListener("click", () => {
		setShareVpadKeysChecked(true);
		if (share.active) saveShare();
	});
	$("share-vpad-keys-none")?.addEventListener("click", () => {
		setShareVpadKeysChecked(false);
		if (share.active) saveShare();
	});
	$("vpad-editor-opacity").oninput = () => {
		settings.vpadOpacity = clamp(Number($("vpad-editor-opacity").value) || 55, 1, 100);
		applyVpadOpacity();
		shareBroadcastVpadSkin(250);
	};
	$("vpad-editor-opacity").onchange = () => {
		saveSettings();
		shareBroadcastVpadSkin();
	};
	$("btn-stop").onclick = () => {
		if (share.isGuest) {
			location.replace(location.pathname);
			return;
		}
		if (isFullscreen()) {
			exitStreamFullscreen();
			return;
		}
		requestDisconnect();
	};

	$("btn-discover").onclick = async () => {
		if (!discoveryEnabled()) return;
		if (discoveryOn) stopDiscovery();
		else await startDiscovery();
		renderHosts();
	};

	$("btn-help").onclick = () => openHelpModal();
	$("help-close")?.addEventListener("click", () => closeHelpModal());
	$("help-modal")?.addEventListener("click", (e) => {
		if (e.target.id === "help-modal") closeHelpModal();
	});

	$("btn-add").onclick = () => {
		const out = $("add-portcheck-out");
		if (out) {
			out.classList.add("hidden");
			out.innerHTML = "";
		}
		$("add-name").value = "";
		$("add-psn").value = "";
		$("add-regist").value = "";
		$("add-morning").value = "";
		$("add-host").value = "";
		fillAddHostIpv4();
		resetAddPortcheck();
		$("add-modal").classList.remove("hidden");
		$("add-host")?.focus();
	};
	$("auth-login-form").onsubmit = (e) => { e.preventDefault(); submitAuth(false); };
	$("auth-register-form").onsubmit = (e) => { e.preventDefault(); submitAuth(true); };
	$("auth-to-register").onclick = () => {
		showAuthPanel("register");
		$("auth-reg-name")?.focus();
	};
	$("auth-to-login").onclick = () => {
		showAuthPanel("login");
		$("auth-email")?.focus();
	};
	$("btn-account").onclick = (e) => {
		e.stopPropagation();
		const menu = $("account-menu");
		if (!menu) return;
		const open = menu.classList.contains("hidden");
		menu.classList.toggle("hidden", !open);
		$("btn-account").setAttribute("aria-expanded", open ? "true" : "false");
	};
	document.addEventListener("click", (e) => {
		const wrap = $("tb-account");
		if (wrap && !wrap.contains(e.target)) closeAccountMenu();
	});
	document.addEventListener("keydown", (e) => {
		if (e.key !== "Escape") return;
		if (!$("home-proxy-modal")?.classList.contains("hidden")) {
			rejectHomeProxy();
			return;
		}
		if (!$("help-modal")?.classList.contains("hidden")) {
			closeHelpModal();
			return;
		}
		if (!$("share-modal")?.classList.contains("hidden")) {
			closeShareModal();
			return;
		}
		if (!$("delete-account-modal")?.classList.contains("hidden")) {
			closeDeleteAccount();
			return;
		}
		if (!$("account-modal")?.classList.contains("hidden")) {
			closeAccountSettings();
			return;
		}
		closeAccountMenu();
	});
	$("btn-account-settings").onclick = (e) => {
		e.stopPropagation();
		openAccountSettings();
	};
	$("btn-share").onclick = (e) => {
		e.stopPropagation();
		openShareModal();
	};
	$("btn-stream-share").onclick = () => openShareModal();
	$("share-banner-manage").onclick = () => openShareModal();
	$("share-banner-ok").onclick = () => {
		share.bannerDismissed = true;
		updateShareBanners();
	};
	bindShareBannerDrag($("share-banner"));
	$("share-close").onclick = closeShareModal;
	$("share-modal").addEventListener("click", (e) => {
		if (e.target.id === "share-modal") closeShareModal();
	});
	$("share-toggle").onclick = async () => {
		share.active = !share.active;
		if (share.active) share.bannerDismissed = false;
		const out = await saveShare();
		if (share.active && out && !streaming) showShareError("share.needStream");
	};
	$("share-regen").onclick = async () => {
		await saveShare({ regenerate: true, active: share.active });
	};
	$("share-copy").onclick = async () => {
		const url = shareLinkUrl();
		if (!url) return;
		try { await navigator.clipboard.writeText(url); } catch {
			$("share-link")?.select();
			document.execCommand("copy");
		}
		showShareError("share.copied");
		$("share-error")?.classList.add("ok");
		setTimeout(() => { $("share-error")?.classList.remove("ok"); showShareError(""); }, 1600);
	};
	["share-opt-video", "share-opt-audio", "share-opt-vpad", "share-opt-gamepad"].forEach((id) => {
		$(id)?.addEventListener("change", () => {
			if (id === "share-opt-vpad" || id === "share-opt-gamepad") syncShareVpadKeysPanel();
			if (share.active) saveShare();
		});
	});
	$("share-opt-keyword-pause")?.addEventListener("change", persistShareKeywordSettings);
	$("share-keywords")?.addEventListener("change", persistShareKeywordSettings);
	$("share-keywords")?.addEventListener("input", () => {
		if ($("share-keywords")) settings.shareKeywords = $("share-keywords").value;
	});
	$("account-close").onclick = closeAccountSettings;
	$("account-modal").addEventListener("click", (e) => {
		if (e.target.id === "account-modal") closeAccountSettings();
	});
	$("account-form").onsubmit = async (e) => {
		e.preventDefault();
		const username = ($("acc-user")?.value || "").trim();
		const currentPassword = $("acc-current")?.value || "";
		const newPassword = $("acc-new")?.value || "";
		const confirm = $("acc-confirm")?.value || "";
		if (newPassword && newPassword !== confirm) {
			showAccountError("account.mismatch");
			return;
		}
		if (!currentPassword) {
			showAccountError("account.needCurrent");
			return;
		}
		showAccountError("");
		const { ok, body } = await cloudRequest("/api/account", {
			method: "POST",
			body: JSON.stringify({
				username,
				currentPassword,
				newPassword
			})
		});
		if (!ok) {
			const map = {
				invalid_credentials: "auth.bad",
				taken: "auth.taken",
				email_taken: "auth.emailTaken",
				weak_password: "auth.weak",
				invalid_username: "auth.invalid",
				invalid_email: "auth.invalidEmail"
			};
			showAccountError(map[body.error] || "auth.bad");
			return;
		}
		cloud.user = body.user || cloud.user;
		refreshAuthUi();
		if ($("acc-current")) $("acc-current").value = "";
		if ($("acc-new")) $("acc-new").value = "";
		if ($("acc-confirm")) $("acc-confirm").value = "";
		showAccountError("account.saved", true);
	};
	$("btn-open-delete").onclick = openDeleteAccount;
	$("delete-account-cancel").onclick = closeDeleteAccount;
	$("delete-account-modal").addEventListener("click", (e) => {
		if (e.target.id === "delete-account-modal") closeDeleteAccount();
	});
	$("delete-account-form").onsubmit = async (e) => {
		e.preventDefault();
		const currentPassword = $("acc-delete-pass")?.value || "";
		if (!currentPassword) {
			showDeleteAccountError("account.needCurrent");
			return;
		}
		showDeleteAccountError("");
		const { ok, body } = await cloudRequest("/api/account/delete", {
			method: "POST",
			body: JSON.stringify({ currentPassword })
		});
		if (!ok) {
			showDeleteAccountError(body.error === "invalid_credentials" ? "auth.bad" : "auth.bad");
			return;
		}
		cloud.user = null;
		clearLocalAccountData();
		location.reload();
	};
	$("btn-logout").onclick = async () => {
		closeAccountMenu();
		await cloudRequest("/api/logout", { method: "POST" });
		cloud.user = null;
		clearLocalAccountData();
		location.reload();
	};
	$("app-version").onclick = () => $("credits-modal").classList.remove("hidden");
	$("credits-close").onclick = () => $("credits-modal").classList.add("hidden");
	$("credits-modal").addEventListener("click", (e) => {
		if (e.target.id === "credits-modal") $("credits-modal").classList.add("hidden");
	});
	$("add-cancel").onclick = () => $("add-modal").classList.add("hidden");
	$("add-portcheck").onclick = () => testAddHostPorts();
	$("add-host").addEventListener("input", () => {
		if (($("add-host").value || "").trim() !== addPortcheckHost) resetAddPortcheck();
	});
	$("add-ok").onclick = () => {
		const host = $("add-host").value.trim();
		if (!host) return;
		if (looksLikeIpv6(host)) return log(t("log.registIpv6"), 0);
		if (!addPortcheckOk || host !== addPortcheckHost) {
			const out = $("add-portcheck-out");
			if (out) {
				out.classList.remove("hidden");
				out.innerHTML = `<p class="portcheck-sum bad">${escapeHtml(t("add.portcheckNeedTest"))}</p>`;
			}
			syncAddOkButton();
			return;
		}
		rememberHost({
			host,
			name: $("add-name").value.trim() || host,
			ps5: $("add-ps5").value === "1",
			registKey: $("add-regist").value.trim(),
			morning: $("add-morning").value.trim(),
			psnId: normalizePsnAccountId($("add-psn")?.value || "")
		});
		$("add-modal").classList.add("hidden");
	};

	$("reg-cancel").onclick = () => $("regist-modal").classList.add("hidden");
	$("reg-lookup").onclick = () => lookupPsnUsername($("reg-psn-user").value);
	$("btn-psn-lookup").onclick = () => lookupPsnUsername($("s-psn-user").value);
	$("reg-ok").onclick = async () => {
		const pin = Number($("reg-pin").value);
		if (!pin) return log(t("log.pinRequired"), 0);
		const ps5 = Number($("regist-modal").dataset.ps5);
		const host = ($("reg-host").dataset.real || $("reg-host").value).trim();
		const psnId = normalizePsnAccountId($("reg-psn").value || psnIdForHost({ addr: host }));
		if (ps5 && !psnIdLooksValid(psnId)) {
			log(t("log.psnHint"), 0);
			$("psn-regist-fields").classList.remove("hidden");
			$("reg-psn").focus();
			return;
		}
		if (looksLikeIpv6(host)) {
			log(t("log.registIpv6"), 0);
			return;
		}
		if (psnId) upsertHostPsn(host, psnId, { name: $("regist-modal").dataset.name, ps5 });
		if (host && !isPrivateIpv4(host)) log(t("log.registLanHint"), 0);
		await ensureWasmRuntime();
		api.regist?.(host, pin, psnId, ps5, 0);
	};

	$("home-proxy-approve")?.addEventListener("click", () => approveHomeProxy());
	$("home-proxy-reject")?.addEventListener("click", () => rejectHomeProxy());
	$("btn-home-proxy-disconnect")?.addEventListener("click", () => disconnectHomeProxy());
	$("btn-pin-ok").onclick = () => {
		api.sessionSetPin($("login-pin").value.trim());
		$("pin-modal").classList.add("hidden");
	};
	$("btn-pin-cancel").onclick = () => $("pin-modal").classList.add("hidden");
	$("btn-clear-hidden").onclick = () => { persistHidden(new Set()); renderHosts(); };
	$("btn-reset-keys").onclick = () => {
		settings.keymap = { ...defaultKeymap };
		settings.mousemap = { ...defaultMousemap };
		saveSettings();
		renderKeymap();
	};
	$("keycap-unbind").onclick = () => {
		if (capturing) clearBinding(capturing.id);
	};
	$("keycap-close").onclick = () => finishKeyCapture(null);
	$("keycap-modal").addEventListener("mousedown", (e) => {
		if (!capturing) return;
		if (e.target.id === "keycap-modal") {
			finishKeyCapture(null);
			return;
		}
		if (e.target.closest("#keycap-close") || e.target.closest("#keycap-unbind") || e.target.closest("[data-remove]")) return;
		if (!e.target.closest("[data-slot='mouse']")) return;
		e.preventDefault();
		applyCapturedCode("Mouse" + e.button);
	});
	$("keycap-modal").addEventListener("wheel", (e) => {
		if (!capturing) return;
		if (!e.target.closest("[data-slot='mouse']")) return;
		e.preventDefault();
		applyCapturedCode(e.deltaY < 0 ? "WheelUp" : "WheelDown");
	}, { passive: false });
	$("s-proxy-mode")?.addEventListener("change", (e) => {
		e.stopPropagation();
		applyProxyModeFromForm();
	});
	$("btn-proxy-apply")?.addEventListener("click", applyCustomProxyFromForm);
	$("s-proxy-url")?.addEventListener("keydown", (e) => {
		if (e.key === "Enter") {
			e.preventDefault();
			applyCustomProxyFromForm();
		}
	});
	$("confirm-modal").addEventListener("click", (e) => {
		if (e.target.id === "confirm-modal") {
			if (confirmDone) confirmDone(false);
			else $("confirm-modal").classList.add("hidden");
		}
	});
	document.addEventListener("visibilitychange", () => {
		if (document.visibilityState === "hidden") {
			onAppSuspend();
			return;
		}
		if (currentView === "welcome") scheduleRenderHosts();
		restartDiscovery();
	});
	window.addEventListener("pageshow", (ev) => {
		if (ev.persisted) {
			location.reload();
			return;
		}
		restartDiscovery();
	});
	window.addEventListener("pagehide", onAppSuspend);

	for (const btn of $("settings-tabs").querySelectorAll("button")) {
		btn.onclick = () => {
			$("settings-tabs").querySelectorAll("button").forEach((b) => b.classList.toggle("active", b === btn));
			document.querySelectorAll(".tab-page").forEach((p) => p.classList.toggle("hidden", p.dataset.page !== btn.dataset.tab));
		};
	}
	$("settings-view").addEventListener("change", () => { applySettingsNow(); });
	$("settings-view").addEventListener("input", () => { applySettingsNow(); });
	$("s-language").addEventListener("change", () => { applySettingsNow(); });

	if (location.hash === "#video") openSettingsTab("video");
	else if (location.hash === "#settings") showView("settings");

	canvas.addEventListener("pointerdown", (e) => {
		if (vpadOn || document.pointerLockElement) return;
		if (settings.mouseTouchEnabled === false) return;
		if (e.button !== 0) return;
		if (settings.mouseStick !== "none" || mouse0Mapped() || document.pointerLockElement) return;
		touch.active = true;
		const r = canvas.getBoundingClientRect();
		touch.x = Math.round((e.clientX - r.left) / r.width * 1919);
		touch.y = Math.round((e.clientY - r.top) / r.height * 1079);
	});
	canvas.addEventListener("pointermove", (e) => {
		if (!touch.active) return;
		const r = canvas.getBoundingClientRect();
		touch.x = Math.round((e.clientX - r.left) / r.width * 1919);
		touch.y = Math.round((e.clientY - r.top) / r.height * 1079);
	});
	canvas.addEventListener("pointerup", () => { touch.active = false; });
	canvas.addEventListener("pointerleave", () => { touch.active = false; });
	$("stream-view").addEventListener("mousemove", onStreamChromeMove);
	$("stream-view").addEventListener("pointerdown", onStreamChromeMove);
	$("stream-view").addEventListener("mouseleave", () => {
		clearTimeout(streamChromeTimer);
		streamChromeTimer = setTimeout(hideStreamChrome, 400);
	});
	canvas.addEventListener("click", () => {
		if (!streaming || vpadOn) return;
		cursorLocked = true;
		tryPointerLock();
	});
	document.addEventListener("pointerlockchange", () => {
		if (!document.pointerLockElement) mouseLook = { x: 0, y: 0 };
		canvas.classList.toggle("cursor-hidden", wantsPointerLock() && !!document.pointerLockElement);
	});
	window.addEventListener("mousemove", (e) => {
		if (!streaming || vpadOn || settings.mouseStick === "none") return;
		if (document.pointerLockElement !== canvas) return;
		const gain = (Number(settings.mouseSens) || 80) / 40;
		mouseLook.x = clamp(mouseLook.x + e.movementX * 0.004 * gain, -1, 1);
		let dy = e.movementY * 0.004 * gain;
		if (settings.mouseInvertY) dy = -dy;
		mouseLook.y = clamp(mouseLook.y + dy, -1, 1);
	});
	window.addEventListener("wheel", (e) => {
		if (capturing) return;
		if (!streaming) return;
		mouseWheel = e.deltaY < 0 ? "WheelUp" : "WheelDown";
		clearTimeout(mouseWheelTimer);
		mouseWheelTimer = setTimeout(() => { mouseWheel = ""; }, 90);
	}, { passive: true });
	canvas.addEventListener("dblclick", (e) => {
		if (!streaming || !settings.dblfs) return;
		e.preventDefault();
		if (isFullscreen()) exitStreamFullscreen();
		else enterStreamFullscreen();
	});
	const onFsChange = () => syncStreamChrome();
	document.addEventListener("fullscreenchange", onFsChange);
	document.addEventListener("webkitfullscreenchange", onFsChange);
	window.addEventListener("resize", () => {
		syncHandheldChrome();
		if (streaming) syncStreamChrome();
	});
	window.addEventListener("orientationchange", () => {
		syncHandheldChrome();
		if (streaming) {
			setTimeout(syncStreamChrome, 80);
			setTimeout(syncStreamChrome, 300);
		}
	});
	screen.orientation?.addEventListener("change", () => {
		if (streaming) {
			syncStreamChrome();
			setTimeout(syncStreamChrome, 250);
		}
	});
	if (window.visualViewport) {
		visualViewport.addEventListener("resize", () => {
			if (streaming) syncStreamChrome();
		});
	}
	window.addEventListener("keydown", (e) => {
		if (capturing) {
			e.preventDefault();
			if (e.code === "Escape") finishKeyCapture(null);
			else applyCapturedCode(e.code);
			return;
		}
		if (e.key === "Escape" && !$("vpad-editor-modal").classList.contains("hidden")) {
			e.preventDefault();
			closeVpadEditor();
			return;
		}
		if (streaming && fireHotkey(e.code)) {
			e.preventDefault();
			return;
		}
		keys.add(e.code);
		if (streaming && ["ArrowUp","ArrowDown","ArrowLeft","ArrowRight"," ","Tab"].includes(e.key)) e.preventDefault();
	});
	window.addEventListener("keyup", (e) => {
		keys.delete(e.code);
		releaseHotkey(e.code);
	});
	window.addEventListener("mousedown", (e) => {
		mouseButtons.add(e.button);
		if (capturing || !streaming) return;
		if (fireHotkey("Mouse" + e.button)) e.preventDefault();
	});
	window.addEventListener("mouseup", (e) => {
		mouseButtons.delete(e.button);
		releaseHotkey("Mouse" + e.button);
	});
	document.addEventListener("pointermove", onVpadPointerMove, { capture: true, passive: false });
	document.addEventListener("pointerup", onVpadPointerUp, { capture: true });
	document.addEventListener("pointercancel", onVpadPointerUp, { capture: true });
	setInterval(pollInput, 8);
	window.addEventListener("gamepadconnected", onGamepadHotplug);
	window.addEventListener("gamepaddisconnected", onGamepadHotplug);
	window.addEventListener("focus", () => { try { navigator.getGamepads?.(); } catch {} refreshPadStatus(); });
}

function bindModule() {
	api = {
		init: Module.cwrap("chiaki_wasm_init", "number", ["string"]),
		fini: Module.cwrap("chiaki_wasm_fini", null, []),
		version: Module.cwrap("chiaki_wasm_version", "string", []),
		netReady: Module.cwrap("chiaki_wasm_net_ready", "number", []),
		discoverStart: Module.cwrap("chiaki_wasm_discover_start", "number", ["string"]),
		discoverStop: Module.cwrap("chiaki_wasm_discover_stop", null, []),
		wakeup: Module.cwrap("chiaki_wasm_wakeup", "number", ["string", "string", "number"]),
		regist: Module.cwrap("chiaki_wasm_regist", "number", ["string", "number", "string", "number", "number"]),
		sessionStart: Module.cwrap("chiaki_wasm_session_start", "number", ["string", "string", "string", "number", "number", "number", "number", "number"]),
		sessionStop: Module.cwrap("chiaki_wasm_session_stop", null, []),
		sessionSetController: Module.cwrap("chiaki_wasm_session_set_controller", "number", ["number", "number", "number", "number", "number", "number", "number", "number", "number", "number"]),
		sessionSetPin: Module.cwrap("chiaki_wasm_session_set_pin", "number", ["string"]),
		sessionGotoBed: Module.cwrap("chiaki_wasm_session_goto_bed", "number", []),
		sessionGoHome: Module.cwrap("chiaki_wasm_session_go_home", "number", []),
		sessionRequestIdr: Module.cwrap("chiaki_wasm_session_request_idr", "number", [])
	};
	Module.onLog = (level, msg) => {
		if (shouldSuppressConnectLog(msg)) return;
		log(msg, level);
	};
	Module.onHost = addDiscovered;
	Module.onHosts = applyHostsSnapshot;
		Module.onVideo = (ptr, size, lost, recovered) => pushVideo(ptr, size, lost, recovered);
	Module.onAudioSettings = (ch, rate) => {
		audio.channels = ch;
		audio.rate = rate;
		pushAudioCfg();
	};
	Module.onAudio = (ptr, samples) => pushAudio(ptr, samples);
	Module.onEvent = (type, code, detail) => {
		if (type === "quit") {
			streaming = false;
			closeSessionGate({ type: "quit", code, detail });
			if (ignoreQuit || connecting || retryingConnect) return;
			log(t("log.sessionEnded", { detail: quitLabel(code, detail) }));
			connecting = false;
			wakingAddrs.clear();
			activeHost = null;
			streamTitleName = "";
			appliedStreamKey = "";
			settingsReturnView = "welcome";
			setStreamOverlay("");
			resetVideoDecoder();
			shareOnStreaming(false);
			showView("welcome").then(() => {
				pauseDiscoveryForStream(false);
				renderHosts();
			});
			return;
		}
		if (type === "connected") {
			ignoreQuit = false;
			retryingConnect = false;
			appliedStreamKey = streamConfigKey();
			closeSessionGate({ type: "connected" });
			setStreamOverlay("");
			shareOnStreaming(true);
		}
		if (type === "pin") $("pin-modal").classList.remove("hidden");
		if (type === "rumble") {
			const pad = pickGamepad();
			if (pad?.vibrationActuator)
				pad.vibrationActuator.playEffect("dual-rumble", { duration: 80, strongMagnitude: code / 255, weakMagnitude: Number(detail) / 255 });
		}
		if (type === "nickname") {
			if (detail) {
				streamTitleName = detail;
				if (activeHost) activeHost.name = detail;
			}
			log(t("log.console", { name: detail }));
			syncDocumentTitle();
		}
	};
	Module.onRegist = (info) => {
		if (!info.ok) return log(t("log.registFailed", { error: info.error || "" }), 0);
		const host = ($("reg-host").dataset.real || $("reg-host").value).trim();
		rememberHost({
			host,
			name: $("regist-modal").dataset.name || info.nickname,
			ps5: !!info.ps5,
			registKey: info.registKey,
			morning: info.morning,
			psnId: normalizePsnAccountId($("reg-psn")?.value || "")
		});
		$("regist-modal").classList.add("hidden");
		log(t("log.registered", { name: info.nickname }));
	};
}

let chiakiScriptP = null;

function wasmRuntimeReady() {
	return typeof Module !== "undefined"
		&& typeof Module.cwrap === "function"
		&& typeof Module._chiaki_wasm_init === "function";
}

function loadChiakiScript() {
	if (wasmRuntimeReady()) return Promise.resolve();
	if (chiakiScriptP) return chiakiScriptP;
	chiakiScriptP = new Promise((resolve, reject) => {
		let done = false;
		const finish = (err) => {
			if (done) return;
			done = true;
			if (err) reject(err);
			else resolve();
		};
		const prevReady = Module.onRuntimeInitialized;
		Module.onRuntimeInitialized = () => {
			window.__chiakiWasmReady = 1;
			try { prevReady?.(); } catch {}
			finish();
		};
		const prevAbort = Module.onAbort;
		Module.onAbort = (what) => {
			window.__chiakiWasmAbort = String(what);
			try { prevAbort?.(what); } catch {}
			finish(new Error(String(what || "wasm abort")));
		};
		const s = document.createElement("script");
		s.src = "chiaki.js";
		s.async = true;
		s.onload = () => {
			if (wasmRuntimeReady()) finish();
		};
		s.onerror = () => finish(new Error("chiaki.js"));
		document.head.appendChild(s);
	});
	return chiakiScriptP;
}

async function startWasmRuntime() {
	if (!window.isSecureContext || typeof SharedArrayBuffer === "undefined") {
		const httpsUrl = `https://${location.hostname}${location.port ? ":" + location.port : ""}/`;
		log(t("log.insecureOrigin", { url: httpsUrl }), 0);
		proxyState = "failed";
		refreshProxyStatus();
		return false;
	}
	if (wasmRuntimeReady() && typeof api.sessionStart === "function")
		return true;
	log(t("log.wasmLoading"));
	try {
		await loadChiakiScript();
		await waitFor(() => wasmRuntimeReady(), 45000);
	} catch (e) {
		const abort = window.__chiakiWasmAbort ? String(window.__chiakiWasmAbort) : "";
		log("WASM: " + (e && e.message ? e.message : e) + (abort ? " / " + abort : ""), 0);
		proxyState = "failed";
		refreshProxyStatus();
		return false;
	}
	bindModule();
	try { $("app-version").textContent = api.version(); } catch {}
	const proto = location.protocol === "https:" ? "wss" : "ws";
	proxyUrl = effectiveProxyUrl() || `${proto}://${location.host}/posix-net`;
	proxyState = "";
	refreshProxyStatus();
	log("Init Chiaki WASM → " + proxyUrl);
	if (api.init(proxyUrl) !== 0) {
		proxyState = "failed";
		refreshProxyStatus();
		return false;
	}
	try {
		await waitFor(() => api.netReady() === 1, 8000);
		proxyState = "connected";
		refreshProxyStatus();
		if (cloud.homeProxy) startDiscovery();
	} catch {
		proxyState = "offline";
		refreshProxyStatus();
		log(t("log.proxyDown"), 0);
	}
	return true;
}

function ensureWasmRuntime() {
	if (!wasmReadyP) {
		wasmReadyP = startWasmRuntime().then((ok) => {
			if (!ok) wasmReadyP = null;
			return ok;
		});
	}
	return wasmReadyP;
}

function scheduleWasmWarmup() {
	const kick = () => {
		if (cloud.homeProxyPending) return;
		ensureWasmRuntime().catch(() => {});
	};
	const onFirstInput = () => {
		document.removeEventListener("pointerdown", onFirstInput, true);
		kick();
	};
	document.addEventListener("pointerdown", onFirstInput, { capture: true, passive: true });
	const idle = typeof requestIdleCallback === "function"
		? (fn) => requestIdleCallback(fn, { timeout: 1800 })
		: (fn) => setTimeout(fn, 400);
	idle(kick);
}

async function boot() {
	document.documentElement.classList.toggle("electron-app", isElectronApp());
	syncHandheldChrome();
	loadSettings();
	bindUi();
	await loadI18n(settings.language);
	applyI18n();
	const guestToken = shareTokenFromHash();
	if (guestToken && !isElectronApp()) {
		await bootGuest(guestToken);
		return;
	}
	await initCloud();
	await loadI18n(settings.language);
	applyI18n();
	await loadShareState();
	renderSavedList();
	renderHosts();
	loadConsoleIcons().then(() => renderHosts()).catch(() => {});
	setBootScreen(false);
	scheduleWasmWarmup();
}

if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", boot);
else boot();
