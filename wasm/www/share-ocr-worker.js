const TESS_JS = "https://cdn.jsdelivr.net/npm/tesseract.js@5.1.1/dist/tesseract.min.js";
const TESS_WORKER = "https://cdn.jsdelivr.net/npm/tesseract.js@5.1.1/dist/worker.min.js";
const TESS_CORE = "https://cdn.jsdelivr.net/npm/tesseract.js-core@5.1.0/tesseract-core-simd.wasm.js";
const TESS_LANG = "https://tessdata.projectnaptha.com/4.0.0";

let tess = null;
let tessP = null;

function ensureTess() {
	if (tess) return Promise.resolve(tess);
	if (tessP) return tessP;
	tessP = (async () => {
		importScripts(TESS_JS);
		const worker = await self.Tesseract.createWorker("fra+eng", 1, {
			logger: () => {},
			workerPath: TESS_WORKER,
			corePath: TESS_CORE,
			langPath: TESS_LANG
		});
		try {
			await worker.setParameters({
				tessedit_pageseg_mode: "11",
				preserve_interword_spaces: "1"
			});
		} catch {}
		tess = worker;
		return tess;
	})().catch((err) => {
		tessP = null;
		throw err;
	});
	return tessP;
}

function foldText(s) {
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

function textHasKey(text, keys) {
	if (!keys || !keys.length || !text) return false;
	const words = foldText(text).split(/\s+/).filter((w) => w.length >= 2);
	for (const k of keys) {
		const compact = foldText(k).replace(/\s+/g, "");
		if (compact && words.includes(compact)) return true;
	}
	return false;
}

async function recognizePsm(worker, src, psm) {
	try {
		await worker.setParameters({ tessedit_pageseg_mode: String(psm) });
	} catch {}
	const out = await worker.recognize(src);
	return (out && out.data && out.data.text) || "";
}

async function recognizeSrc(worker, src, keys) {
	const sparse = await recognizePsm(worker, src, 11);
	if (textHasKey(sparse, keys)) return sparse;
	const block = await recognizePsm(worker, src, 6);
	return [sparse, block].filter((t) => String(t).trim()).join(" ");
}

self.onmessage = async (ev) => {
	const data = ev.data || {};
	const src = data.bmp || data.blob;
	if (!src) {
		self.postMessage({ err: true });
		return;
	}
	try {
		const worker = await ensureTess();
		const text = await recognizeSrc(worker, src, data.keys);
		try { if (data.bmp && data.bmp.close) data.bmp.close(); } catch {}
		self.postMessage({ text: text || "", err: false });
	} catch {
		try { if (data.bmp && data.bmp.close) data.bmp.close(); } catch {}
		self.postMessage({ err: true });
	}
};
