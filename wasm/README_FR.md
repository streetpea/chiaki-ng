# Chiaki-NG Web Edition

Port **navigateur + Electron** de Chiaki-NG : `chiaki-lib` tourne en WebAssembly, le réseau POSIX (TCP/UDP vers la PS4/PS5) passe par un **proxy Node.js**.

Client web en ligne (DEMO) : [https://chiaki.csphere.fr/](https://chiaki.csphere.fr/)

Build : voir [`Build_WASM.md`](Build_WASM.md).

---

## Mises en place

### 1. WASM + Node.js (client web / serveur)

Le navigateur n’a pas de sockets POSIX. La page charge l’UI (`wasm/www/`) et le binaire WASM. 
Node sert les fichiers (en-têtes **COOP/COEP** obligatoires pour les pthreads) et relaie `/posix-net` en WebSocket vers TCP/UDP.

Lancement local : `node wasm/proxy/server.mjs` → http://127.0.0.1:8090/ (ou le port du `.env`).

### 2. Client Electron (Windows / Linux / macOS)

Même stack (WASM + proxy + UI) dans une fenêtre Chromium. Un `.exe` / AppImage / `.dmg` démarre le proxy en local et ouvre l’interface : pas besoin d’un navigateur ni d’un SERVEUR.

- Sources : `wasm/electron/`
- Sortie : `wasm/build/`
- Commandes : `.\scripts\build-electron.ps1` / `./scripts/build-electron.sh`

Le mode **serveur web n’est pas retiré**. Electron ne lit **pas** le `.env` du SERVEUR : il utilise un `.env` dans le profil utilisateur.

### 3. Proxy Node — rôle et ports

Le proxy fait trois choses :

1. Servir l’UI et le `.wasm` avec COOP/COEP (`SharedArrayBuffer`).
2. Relayer chaque paquet POSIX du WASM vers la console (`ws://…/posix-net`).
3. APIs (`/api/…`) : comptes, consoles, test de port, partage, etc.

**Côté machine qui héberge Node** (SERVEUR ou PC) :

| Port | Usage |
|---|---|
| `CHIAKI_WASM_PORT` (**8090**) TCP | HTTP(S) + WebSocket UI et posix-net |
| **443** TCP | HTTPS public (ex. [chiaki.csphere.fr](https://chiaki.csphere.fr/)) via nginx / reverse-proxy |

Hors `localhost`, le WASM pthread exige **HTTPS**.

**Routeur / firewall / console** (Remote Play, IPv4) :

| Port | Proto | Rôle |
|---|---|---|
| **9295** | TCP | Session Remote Play — **testé** avant d’ajouter une console |
| **9296** / **9297** | UDP | Flux vidéo / audio (à ouvrir en NAT si tu joues hors LAN) |
| **987** | UDP | Découverte PS4 (LAN) |
| **9302** | UDP | Découverte PS5 (LAN) |

Sans **TCP 9295** ouvert jusqu’à la console, l’ajout manuel refuse. En LAN, découverte + 9295 suffisent souvent. En WAN, forward **9295 TCP** (et en pratique les UDP stream) vers la PS4/PS5.

Electron n’expose le proxy que sur `127.0.0.1` (port **18780** par défaut) : aucun port à ouvrir sur Internet pour l’exe lui-même. La console, elle, doit rester joignable (LAN ou 9295).

---

## Optimisations et ajouts

### Fichier `.env` (serveur et Electron)

Un seul format pour régler Node **sans recompiler**. Copier `.env.example` vers `.env` à la racine (pour serveur) :

```env
CHIAKI_AUTH_ENABLED=true
CHIAKI_AUTH_ALLOW_REGISTER=true
CHIAKI_ADMIN_USER=
CHIAKI_ADMIN_EMAIL=
CHIAKI_ADMIN_PASSWORD=
CHIAKI_DB_DIR=./db
CHIAKI_DB_NAME=chiaki.sqlite
CHIAKI_SESSION_DAYS=30
CHIAKI_SESSION_SECRET=change-me
CHIAKI_MAX_HOSTS=32
CHIAKI_WASM_PORT=8090
```

Autres variables utiles : `CHIAKI_WASM_ROOT`, `CHIAKI_WASM_WWW`, `CHIAKI_WASM_BIND`, `CHIAKI_WASM_LAN_HTTPS`, `CHIAKI_DISCOVERY_ENABLED`, `CHIAKI_ENV_FILE`.

| | Serveur / SERVEUR | Electron |
|---|---|---|
| Fichier | `.env` à la racine du projet (ou `cwd`) | `%AppData%/Chiaki-NG Web/.env` (Windows) — **pas** celui du SERVEUR |
| Auth | à activer en prod (`CHIAKI_AUTH_ENABLED=true`) | désactivée par défaut (usage local) |
| Port | 8080 / 8090 / 443 | 18780 en local seulement |


### Authentification

Protège l’accès aux consoles enregistrées (SQLite). Connexion / création de compte, sessions cookie, compte admin optionnel au premier démarrage. Les consoles et réglages suivent l’utilisateur sur tous les appareils.

Désactiver l’auth (`CHIAKI_AUTH_ENABLED=false`) : usage local / Electron sans page de login.

### Partage de stream (avec l’auth)

L’hôte connecté peut générer un **lien spectateur**. Droits au choix :

- vidéo
- son
- manette physique
- la manette virtuelle suit le stream de l’hôte

Les invités reçoivent le flux (WebRTC) sans les clés Remote Play de la console.

### Touches et souris

Onglet **Keys** : remap **clavier** et **souris** séparés (boutons DualShock / DualSense, sticks, gâchettes, PS, pavé tactile). Capture au clic, reset des défauts, raccourcis stream (arrêt, restart, curseur / **F8**). Souris : stick droit ou gauche, sensibilité, inversion Y, verrouillage du curseur sur le stream.

### Manette virtuelle (smartphone)

Overlay tactile : tap court envoyé tout de suite, disposition / taille en éditeur plein écran, transparence. Pensée pour le mobile ; affichage depuis la barre du stream.

### Langues

Interface **français** et **anglais** (onglet Général). Fichiers `wasm/www/i18n/`.

### Autres

- Stream : WebCodecs H.264/H.265, AudioWorklet, IDR, WASM lazy-load, plein écran Android, × = quitter le FS seulement
- Défauts stream : **1080p**, **60 ips**, **15000 kbps**, H265
- Ajout console : IPv4, test TCP 9295, ID PSN via [psntools](https://www.psntools.com/) (bouton Rechercher)
- Découverte LAN, réveil, enregistrement PIN

---

