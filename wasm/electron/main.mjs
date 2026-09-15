#!/usr/bin/env node
import { app, BrowserWindow, Menu, dialog, shell } from "electron";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));

app.commandLine.appendSwitch("js-flags", "--experimental-sqlite");
app.commandLine.appendSwitch("autoplay-policy", "no-user-gesture-required");
app.commandLine.appendSwitch("enable-features", "SharedArrayBuffer");

const gotLock = app.requestSingleInstanceLock();
if (!gotLock) {
	app.quit();
}

app.on("second-instance", () => {
	if (!mainWindow) return;
	if (mainWindow.isMinimized()) mainWindow.restore();
	mainWindow.focus();
});

function repoRoot() {
	return path.resolve(__dirname, "..", "..");
}

function wasmDir() {
	if (app.isPackaged)
		return path.join(process.resourcesPath, "chiaki-wasm");
	return path.join(repoRoot(), "build-wasm", "wasm");
}

function wwwDir() {
	if (app.isPackaged)
		return wasmDir();
	return path.join(repoRoot(), "wasm", "www");
}

function proxyServerPath() {
	if (app.isPackaged)
		return path.join(wasmDir(), "server.mjs");
	return path.join(repoRoot(), "wasm", "proxy", "server.mjs");
}

function wasmReady(dir) {
	return fs.existsSync(path.join(dir, "chiaki.wasm")) && fs.existsSync(path.join(dir, "chiaki.js"));
}

let serverHandle = null;
let mainWindow = null;
let quitting = false;

async function showFatal(title, message) {
	if (app.isReady())
		await dialog.showMessageBox({ type: "error", title, message, buttons: ["OK"] });
	else
		console.error(title, message);
}

async function createWindow(url) {
	mainWindow = new BrowserWindow({
		width: 1280,
		height: 800,
		minWidth: 800,
		minHeight: 560,
		backgroundColor: "#000000",
		autoHideMenuBar: true,
		webPreferences: {
			preload: path.join(__dirname, "preload.cjs"),
			contextIsolation: true,
			nodeIntegration: false,
			sandbox: true,
			spellcheck: false
		}
	});
	mainWindow.webContents.setWindowOpenHandler(({ url: target }) => {
		if (target.startsWith("http://127.0.0.1") || target.startsWith("https://127.0.0.1"))
			return { action: "allow" };
		shell.openExternal(target);
		return { action: "deny" };
	});
	mainWindow.on("closed", () => { mainWindow = null; });
	await mainWindow.loadURL(url);
}

async function start() {
	if (process.platform === "darwin") {
		Menu.setApplicationMenu(Menu.buildFromTemplate([
			{ role: "appMenu" },
			{ role: "editMenu" },
			{ role: "viewMenu" },
			{ role: "windowMenu" }
		]));
	} else {
		Menu.setApplicationMenu(null);
	}

	const root = wasmDir();
	const www = wwwDir();
	const proxyFile = proxyServerPath();

	if (!wasmReady(root)) {
		await showFatal(
			"WASM manquant",
			"Compilez d’abord le client WASM (scripts/build-wasm.ps1 ou scripts/build-wasm.sh).\n\nDossier attendu :\n" + root
		);
		app.quit();
		return;
	}
	if (!fs.existsSync(proxyFile)) {
		await showFatal("Proxy manquant", "Fichier introuvable :\n" + proxyFile);
		app.quit();
		return;
	}

	process.env.CHIAKI_WASM_ROOT = root;
	process.env.CHIAKI_WASM_WWW = www;
	process.env.CHIAKI_WASM_BIND = "127.0.0.1";
	process.env.CHIAKI_WASM_LAN_HTTPS = "0";
	if (!process.env.CHIAKI_WASM_PORT)
		process.env.CHIAKI_WASM_PORT = "18780";
	process.env.CHIAKI_DB_DIR = app.getPath("userData");
	if (!process.env.CHIAKI_ENV_FILE)
		process.env.CHIAKI_ENV_FILE = path.join(app.getPath("userData"), ".env");

	const mod = await import(pathToFileURL(proxyFile).href);
	const boot = typeof mod.startServers === "function"
		? mod.startServers
		: typeof mod.startServer === "function"
			? mod.startServer
			: null;
	if (typeof boot !== "function") {
		const port = Number(process.env.CHIAKI_WASM_PORT || 18780);
		serverHandle = {
			url: `http://127.0.0.1:${port}/`,
			port,
			async close() {}
		};
		console.warn("Proxy déjà démarré (export startServers absent). Fenêtre: " + serverHandle.url);
	} else {
		serverHandle = await boot();
	}
	await createWindow(serverHandle.url);
}

if (gotLock) {
	app.whenReady().then(start).catch(async (err) => {
		await showFatal("Chiaki-NG Web", err && err.stack ? err.stack : String(err));
		app.quit();
	});
}

app.on("window-all-closed", () => {
	if (process.platform !== "darwin")
		app.quit();
});

app.on("activate", () => {
	if (mainWindow === null && serverHandle)
		createWindow(serverHandle.url);
});

app.on("before-quit", (e) => {
	if (quitting || !serverHandle) return;
	e.preventDefault();
	quitting = true;
	const handle = serverHandle;
	serverHandle = null;
	Promise.resolve(handle.close()).finally(() => app.exit(0));
});
