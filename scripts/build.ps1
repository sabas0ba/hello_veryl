# Veryl -> yosys -> nextpnr-himbaechel -> gowin_pack for Tang Nano 9K
# Output: build/top.fs
#Requires -Version 5.1
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
$Suite    = Join-Path $RepoRoot 'tools\oss-cad-suite'
if (-not (Test-Path (Join-Path $Suite 'bin\yosys.exe'))) {
    throw "toolchain not found. Run scripts\setup-toolchain.ps1 first"
}
if (-not (Get-Command veryl -ErrorAction SilentlyContinue)) {
    throw "veryl not found in PATH. See https://veryl-lang.org/"
}
# PATH is modified for this process only
$env:PATH = (Join-Path $Suite 'bin') + ';' + (Join-Path $Suite 'lib') + ';' + $env:PATH

Push-Location $RepoRoot
try {
    New-Item -ItemType Directory -Force build | Out-Null

    veryl build
    if ($LASTEXITCODE -ne 0) { throw "veryl build failed" }

    # Veryl emits the source list with \\?\-prefixed absolute Windows paths.
    # slang command files glob-expand '?' and treat '\' as an escape character,
    # so rewrite to repo-relative forward-slash paths (cwd = repo root); the
    # same list then also works inside the container flow
    ((Get-Content hello_veryl.f) -replace '^\\\\\?\\', '') |
        ForEach-Object { $_.Replace("$RepoRoot\", '').Replace('\', '/') } |
        Set-Content -Encoding Ascii build\sources.f

    yosys -s scripts\synth.ys
    if ($LASTEXITCODE -ne 0) { throw "yosys failed" }

    nextpnr-himbaechel `
        --json build\top_synth.json `
        --write build\top_pnr.json `
        --device 'GW1NR-LV9QN88PC6/I5' `
        --vopt family=GW1N-9C `
        --vopt cst=constraints\tangnano9k.cst `
        --freq 27
    if ($LASTEXITCODE -ne 0) { throw "nextpnr failed" }

    gowin_pack -d GW1N-9C -o build\top.fs build\top_pnr.json
    if ($LASTEXITCODE -ne 0) { throw "gowin_pack failed" }

    Write-Host "Bitstream generated: build\top.fs"
} finally {
    Pop-Location
}
