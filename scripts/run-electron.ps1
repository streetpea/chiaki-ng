param(
	[ValidateSet("start", "pack", "dist", "dist:win", "dist:linux", "dist:mac")]
	[string]$Command = "start"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$ElectronDir = Join-Path $Root "wasm\electron"
$WasmDir = Join-Path $Root "build-wasm\wasm"

if (-not (Test-Path (Join-Path $WasmDir "chiaki.wasm"))) {
	Write-Error "WASM manquant. Compilez d'abord: .\scripts\build-wasm.ps1`nAttendu: $WasmDir\chiaki.wasm"
}

Set-Location $ElectronDir
if (-not (Test-Path "node_modules")) {
	Write-Host "npm install (wasm/electron) ..."
	npm install
	if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

Write-Host "electron $Command ..."
npm run $Command
exit $LASTEXITCODE
