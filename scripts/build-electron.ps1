param(
	[ValidateSet("win", "linux", "mac", "current")]
	[string]$Target = "current"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$ElectronDir = Join-Path $Root "wasm\electron"
$OutDir = Join-Path $Root "wasm\build"
$WasmDir = Join-Path $Root "build-wasm\wasm"
$WwwDir = Join-Path $Root "wasm\www"
$ProxyDir = Join-Path $Root "wasm\proxy"

if (-not (Test-Path (Join-Path $WasmDir "chiaki.wasm"))) {
	Write-Error "WASM manquant. Compilez d'abord: .\scripts\build-wasm.ps1`nAttendu: $WasmDir\chiaki.wasm"
}

if ($Target -eq "current") {
	$Target = "win"
}

function Stop-ChiakiWeb {
	cmd /c "taskkill /F /IM chiaki-ng-web.exe >nul 2>&1"
	Get-CimInstance Win32_Process -ErrorAction SilentlyContinue |
		Where-Object { $_.ExecutablePath -and $_.ExecutablePath -like "$OutDir\*" } |
		ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
}

function Sync-ElectronStage {
	$stage = Join-Path $ElectronDir ".stage"
	if (Test-Path $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
	New-Item -ItemType Directory -Force -Path $stage | Out-Null
	Get-ChildItem $WasmDir -File | Where-Object { $_.Name -like "chiaki*" } | Copy-Item -Destination $stage -Force
	Copy-Item -Path (Join-Path $WwwDir "*") -Destination $stage -Recurse -Force
	Copy-Item -Path (Join-Path $ProxyDir "*") -Destination $stage -Recurse -Force
	if (Select-String -Path (Join-Path $stage "index.html") -Pattern "s-stream-auto" -Quiet) {
		Write-Error "UI stage encore ancienne (s-stream-auto present). Verifiez wasm/www/index.html"
	}
	Write-Host "Stage UI pret: $stage"
}

Set-Location $ElectronDir
if (-not (Test-Path "node_modules")) {
	Write-Host "npm install (wasm/electron) ..."
	npm install
	if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
Stop-ChiakiWeb
Sync-ElectronStage

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$packName = "pack-$stamp"
$packRel = "../build/$packName"
$packAbs = Join-Path $OutDir $packName

$ebArgs = @("--publish", "never", "--config.directories.output=$packRel")
switch ($Target) {
	"win" { $ebArgs = @("--win") + $ebArgs }
	"linux" { $ebArgs = @("--linux") + $ebArgs }
	"mac" { $ebArgs = @("--mac") + $ebArgs }
}

Write-Host "Packaging Electron ($Target) -> $packAbs"
npx --yes electron-builder @ebArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Get-ChildItem $packAbs -File -ErrorAction SilentlyContinue | ForEach-Object {
	Copy-Item -Force $_.FullName (Join-Path $OutDir $_.Name)
}

Write-Host "OK."
Write-Host " App depliee : $packAbs\win-unpacked\chiaki-ng-web.exe"
Write-Host " Installateurs copies dans : $OutDir"
Get-ChildItem $OutDir -File | ForEach-Object { Write-Host " - $($_.Name)" }
exit 0
