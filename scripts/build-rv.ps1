# TopRv (RV32I ソフトコア) のコンテナビルド
#   veryl build -> yosys -> nextpnr -> gowin_pack
# 出力: build/top_rv.fs
# 既存 Top のビルドは build-container.ps1．
#Requires -Version 5.1
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
$Image    = 'hello-veryl-fpga'

if (-not (Get-Command podman -ErrorAction SilentlyContinue)) {
    throw "podman not found in PATH"
}

Push-Location $RepoRoot
try {
    New-Item -ItemType Directory -Force build | Out-Null

    podman build -t $Image -f container\Containerfile container
    if ($LASTEXITCODE -ne 0) { throw "podman build failed" }

    podman run --rm --network none -v "${RepoRoot}:/work" $Image veryl build
    if ($LASTEXITCODE -ne 0) { throw "veryl build failed" }

    ((Get-Content hello_veryl.f) -replace '^/work/', '') |
        Set-Content -Encoding Ascii build\sources.f

    podman run --rm --network none -v "${RepoRoot}:/work" $Image bash scripts/synth_pnr_rv.sh
    if ($LASTEXITCODE -ne 0) { throw "container synthesis failed" }
} finally {
    Pop-Location
}
