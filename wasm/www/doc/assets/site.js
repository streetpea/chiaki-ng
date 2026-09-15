const I18N = {
	fr: {
		site: "Chiaki-NG Web",
		home: "Accueil",
		setup: "Setup",
		search: "Rechercher",
		searchEmpty: "Aucun résultat",
		toc: "Sur cette page",
		themeLight: "Passer en thème clair",
		themeDark: "Passer en thème sombre",
		copy: "Copier",
		copied: "Copié",
		official: "GitHub Chiaki-NG Web"
	},
	en: {
		site: "Chiaki-NG Web",
		home: "Home",
		setup: "Setup",
		search: "Search",
		searchEmpty: "No results",
		toc: "On this page",
		themeLight: "Switch to light mode",
		themeDark: "Switch to dark mode",
		copy: "Copy",
		copied: "Copied",
		official: "Chiaki-NG Web on GitHub"
	}
};

const NAV = {
	fr: [
		{ id: "index", href: "index.html", title: "Vue d’ensemble" },
		{ id: "connexion", href: "connexion.html", title: "Modes de connexion" },
		{ id: "ports", href: "ports.html", title: "Ouvrir les ports" },
		{ id: "home-proxy", href: "home-proxy.html", title: "Chiaki Home Proxy" },
		{ id: "consoles", href: "consoles.html", title: "Consoles" },
		{ id: "compte", href: "compte.html", title: "Compte" },
		{ id: "partage", href: "partage.html", title: "Partage" },
		{ id: "interface", href: "interface.html", title: "Interface" },
		{ id: "limites", href: "limites.html", title: "Limites vs chiaki-ng" }
	],
	en: [
		{ id: "index", href: "index.html", title: "Overview" },
		{ id: "connection", href: "connection.html", title: "Connection modes" },
		{ id: "ports", href: "ports.html", title: "Open ports" },
		{ id: "home-proxy", href: "home-proxy.html", title: "Chiaki Home Proxy" },
		{ id: "consoles", href: "consoles.html", title: "Consoles" },
		{ id: "account", href: "account.html", title: "Account" },
		{ id: "sharing", href: "sharing.html", title: "Sharing" },
		{ id: "interface", href: "interface.html", title: "Interface" },
		{ id: "limits", href: "limits.html", title: "Limits vs chiaki-ng" }
	]
};

const SEARCH = {
	fr: [
		["index", "Vue d’ensemble Remote Play navigateur"],
		["connexion", "LAN cloud sans proxy Home Proxy NAT"],
		["ports", "9295 9296 9297 9302 987 redirection NAT CGNAT IPv4"],
		["home-proxy", "Chiaki Home Proxy agent PC LAN aucun port"],
		["consoles", "découverte PSN base64 masquer supprimer PIN"],
		["compte", "e-mail mot de passe synchronisation"],
		["partage", "WebRTC spectateurs mot-clé"],
		["interface", "réglages vpad clavier souris"],
		["limites", "holepunch PSN chiaki-ng bureau"]
	],
	en: [
		["index", "Overview Remote Play browser"],
		["connection", "LAN cloud without proxy Home Proxy NAT"],
		["ports", "9295 9296 9297 9302 987 port forwarding CGNAT IPv4"],
		["home-proxy", "Chiaki Home Proxy agent PC LAN no ports"],
		["consoles", "discovery PSN base64 hide delete PIN"],
		["account", "email password sync"],
		["sharing", "WebRTC viewers keyword"],
		["interface", "settings vpad keyboard mouse"],
		["limits", "holepunch PSN chiaki-ng desktop"]
	]
};

function icon(name) {
	const paths = {
		menu: '<path d="M3 6h18v2H3V6m0 5h18v2H3v-2m0 5h18v2H3v-2Z"/>',
		search: '<path d="M9.5 3A6.5 6.5 0 0 1 16 9.5c0 1.61-.59 3.09-1.56 4.23l.27.27h.79l5 5-1.5 1.5-5-5v-.79l-.27-.27A6.516 6.516 0 0 1 9.5 16 6.5 6.5 0 0 1 3 9.5 6.5 6.5 0 0 1 9.5 3m0 2C7 5 5 7 5 9.5S7 14 9.5 14 14 12 14 9.5 12 5 9.5 5Z"/>',
		sun: '<path d="M12 7a5 5 0 0 1 5 5 5 5 0 0 1-5 5 5 5 0 0 1-5-5 5 5 0 0 1 5-5m0 2a3 3 0 0 0-3 3 3 3 0 0 0 3 3 3 3 0 0 0 3-3 3 3 0 0 0-3-3m0-5.12.76 2.07L15 5l-1.88.76L12.36 8l-.76-2.07L9.72 5l1.88-.76L12 3.88M4.22 5.64l1.42-1.42 1.76 1.17-.7 2.02-2.12-.2-.7-1.93m15.56 0-.7 1.93-2.12.2-.7-2.02 1.76-1.17 1.42 1.42M1.06 11l2.12.2.7 2.02-1.76 1.17-1.42-1.42.36-1.97M20.12 11l.36 1.97-1.42 1.42-1.76-1.17.7-2.02 2.12-.2M4.22 18.36l.7-1.93 2.12-.2.7 2.02-1.76 1.17-1.42-1.42m15.56 0-1.42 1.42-1.76-1.17.7-2.02 2.12.2.7 1.93M12 16.12l.76 2.07L15 19l-1.88.76L12.36 22l-.76-2.07L9.72 19l1.88-.76L12 16.12Z"/>',
		moon: '<path d="M12 3c.13 0 .26 0 .39.02C10.49 4.45 9 6.92 9 9.78 9 13.65 12.2 16.78 16.07 16.78c.96 0 1.87-.17 2.71-.47C17.67 19.08 15.03 21 12 21 7.03 21 3 16.97 3 12S7.03 3 12 3Z"/>',
		github: '<path d="M12 2A10 10 0 0 0 2 12c0 4.42 2.87 8.17 6.84 9.5.5.08.66-.23.66-.5v-1.69c-2.77.6-3.36-1.34-3.36-1.34-.46-1.16-1.11-1.47-1.11-1.47-.91-.62.07-.6.07-.6 1 .07 1.53 1.03 1.53 1.03.87 1.52 2.34 1.07 2.91.83.09-.65.35-1.09.63-1.34-2.22-.25-4.55-1.11-4.55-4.92 0-1.11.38-2 1.03-2.71-.1-.25-.45-1.29.1-2.64 0 0 .84-.27 2.75 1.02.79-.22 1.65-.33 2.5-.33.85 0 1.71.11 2.5.33 1.91-1.29 2.75-1.02 2.75-1.02.55 1.35.2 2.39.1 2.64.65.71 1.03 1.6 1.03 2.71 0 3.82-2.34 4.66-4.57 4.91.36.31.69.92.69 1.85V21c0 .27.16.59.67.5C19.14 20.16 22 16.42 22 12A10 10 0 0 0 12 2Z"/>'
	};
	return `<svg viewBox="0 0 24 24" aria-hidden="true">${paths[name] || ""}</svg>`;
}

function applyTheme() {
	const saved = localStorage.getItem("chiaki-docs-theme");
	const dark = saved !== "default";
	document.documentElement.dataset.theme = dark ? "slate" : "default";
	return dark;
}

function asset(path) {
	const root = document.body.dataset.root || ".";
	return root.replace(/\/$/, "") + "/" + path;
}

function pageHref(lang, id) {
	const item = NAV[lang].find((p) => p.id === id);
	return item ? item.href : "index.html";
}

function build() {
	const isHome = document.body.dataset.home === "1";
	const lang = isHome
		? ((navigator.language || "fr").toLowerCase().startsWith("en") ? "en" : "fr")
		: (document.documentElement.lang === "en" ? "en" : "fr");
	const t = I18N[lang];
	const page = document.body.dataset.page || "index";
	const root = document.body.dataset.root || ".";
	const setupBase = isHome ? lang + "/" : "./";
	const homeHref = isHome ? "index.html" : "../index.html";
	const article = document.querySelector("article.md-typeset");
	if (!article) return;

	applyTheme();
	const dark = document.documentElement.dataset.theme === "slate";

	const navItems = NAV[lang].map((p) => {
		const href = isHome ? setupBase + p.href : p.href;
		return `<li><a href="${href}" class="${!isHome && p.id === page ? "active" : ""}">${p.title}</a></li>`;
	}).join("");

	const otherPage = ({
		index: "index",
		connexion: "connection",
		connection: "connexion",
		ports: "ports",
		"home-proxy": "home-proxy",
		consoles: "consoles",
		compte: "account",
		account: "compte",
		partage: "sharing",
		sharing: "partage",
		interface: "interface",
		limites: "limits",
		limits: "limites"
	})[page] || "index";
	const langHref = (target) => {
		if (isHome) return target + "/index.html";
		if (lang === target) return pageHref(target, page);
		return "../" + target + "/" + pageHref(target, otherPage);
	};

	const shell = document.createElement("div");
	shell.innerHTML = `
<header class="md-header">
	<div class="md-header__inner">
		<a class="md-header__topic" href="${homeHref}">
			<img src="${asset("assets/logo-mark.svg")}" alt="">
			<span>${t.site}</span>
		</a>
		<div class="md-header__title"></div>
		<div class="md-search" id="search-wrap">
			<span class="md-search__icon">${icon("search")}</span>
			<input class="md-search__input" id="search" placeholder="${t.search}" autocomplete="off">
			<div class="md-search__drop" id="search-drop"></div>
		</div>
		<nav class="lang" aria-label="Language">
			<a href="${langHref("fr")}" class="${lang === "fr" && !isHome ? "active" : ""}">FR</a>
			<a href="${langHref("en")}" class="${lang === "en" && !isHome ? "active" : ""}">EN</a>
		</nav>
		<button class="md-header__button" id="theme-toggle" title="${dark ? t.themeLight : t.themeDark}">${icon(dark ? "sun" : "moon")}</button>
		<a class="md-source" href="https://github.com/CSystem77/Chiaki-NG-Web" target="_blank" rel="noopener" title="${t.official}">${icon("github")}</a>
	</div>
</header>
<div class="md-container" id="drawer">
	<aside class="md-sidebar">
		<nav>
			<p class="md-nav__title">${t.setup}</p>
			<ul class="md-nav__list">${navItems}</ul>
		</nav>
	</aside>
	<div class="md-main">
		<div class="md-content" id="content-slot"></div>
		<aside class="md-toc" id="toc-slot"></aside>
	</div>
</div>`;

	while (shell.firstChild) document.body.insertBefore(shell.firstChild, article);
	document.getElementById("content-slot").appendChild(article);

	article.querySelectorAll("h2[id], h3[id]").forEach((h) => {
		if (!h.id) h.id = h.textContent.trim().toLowerCase().replace(/\s+/g, "-");
	});
	article.querySelectorAll("h2, h3").forEach((h) => {
		if (!h.id) h.id = slug(h.textContent);
	});
	const tocLinks = [...article.querySelectorAll("h2, h3")].map((h) =>
		`<a href="#${h.id}">${h.textContent}</a>`
	).join("");
	const toc = document.getElementById("toc-slot");
	if (tocLinks) toc.innerHTML = `<nav><p class="md-nav__title">${t.toc}</p>${tocLinks}</nav>`;
	else toc.remove();

	article.querySelectorAll("pre").forEach((pre) => {
		const btn = document.createElement("button");
		btn.type = "button";
		btn.className = "copy";
		btn.textContent = t.copy;
		btn.onclick = async () => {
			try { await navigator.clipboard.writeText(pre.querySelector("code")?.textContent || pre.textContent); }
			catch {}
			btn.textContent = t.copied;
			setTimeout(() => { btn.textContent = t.copy; }, 1200);
		};
		pre.appendChild(btn);
	});

	document.querySelectorAll(".tabbed").forEach((box) => {
		const labels = [...box.querySelectorAll(".tabbed-labels button")];
		const panels = [...box.querySelectorAll(".tabbed-panel")];
		labels.forEach((btn, i) => {
			btn.onclick = () => {
				labels.forEach((b) => b.classList.remove("active"));
				panels.forEach((p) => p.classList.remove("active"));
				btn.classList.add("active");
				panels[i]?.classList.add("active");
			};
		});
	});

	document.getElementById("theme-toggle").onclick = () => {
		const next = document.documentElement.dataset.theme === "slate" ? "default" : "slate";
		localStorage.setItem("chiaki-docs-theme", next);
		location.reload();
	};

	const input = document.getElementById("search");
	const drop = document.getElementById("search-drop");
	input.addEventListener("input", () => {
		const q = input.value.trim().toLowerCase();
		if (!q) { drop.classList.remove("open"); return; }
		const hits = SEARCH[lang].filter((row) =>
			row[1].toLowerCase().includes(q) || NAV[lang].find((p) => p.id === row[0])?.title.toLowerCase().includes(q)
		);
		drop.innerHTML = hits.length
			? hits.map((row) => {
				const p = NAV[lang].find((x) => x.id === row[0]);
				const href = isHome ? lang + "/" + p.href : p.href;
				return `<a href="${href}">${p.title}</a>`;
			}).join("")
			: `<div class="empty">${t.searchEmpty}</div>`;
		drop.classList.add("open");
	});
	input.addEventListener("focus", () => document.getElementById("search-wrap").classList.add("expand"));
	document.addEventListener("click", (e) => {
		if (!document.getElementById("search-wrap").contains(e.target)) drop.classList.remove("open");
	});
}

function slug(s) {
	return String(s || "").toLowerCase().normalize("NFD").replace(/[\u0300-\u036f]/g, "")
		.replace(/[^a-z0-9]+/g, "-").replace(/^-|-$/g, "") || "section";
}

if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", build);
else build();
