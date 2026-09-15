# Chiaki Home Proxy

Console : le flux Remote Play passe par **votre PC** (LAN vers la PS4/PS5).  
Connexion sortante vers chiaki.csphere.fr — **aucun port à ouvrir** sur la box.

## Pour l’utilisateur

1. Téléchargez le programme depuis Paramètres → Config sur le site.
2. Lancez-le (double-clic).
3. Saisissez l’**e-mail** et le **mot de passe** du compte Chiaki.
4. Laissez la fenêtre ouverte, puis jouez sur le site avec le même compte.

Aucun fichier `.env` n’est nécessaire.

## Build (développeur)

### Windows

```bat
cmake -S . -B build
cmake --build build --config Release
```

Le binaire est copié vers `wasm/www/downloads/chiaki-proxy-windows.exe` (téléchargeable depuis le site).

### Linux

Le dossier `build/` généré sous Windows **ne peut pas** être réutilisé. Sur le VPS :

```bash
sudo apt install build-essential cmake libssl-dev
cd /home/Chiaki/Chiaki/proxy
rm -rf build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Le binaire est `build/chiaki-proxy` (copie aussi vers `wasm/www/downloads/chiaki-proxy-linux`).

Lancer :

```bash
./build/chiaki-proxy
```

### macOS

```bash
brew install cmake openssl@3
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Optionnel : `CHIAKI_CLOUD_URL` dans un `.env` à côté de l’exe pour un serveur autre que chiaki.csphere.fr.
