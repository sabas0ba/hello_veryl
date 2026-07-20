# Container-based build: veryl runs on the host, synthesis/PnR/pack run in a
# podman container (Windows-native toolchain is unreliable under Smart App
# Control, which blocks unsigned binaries such as nextpnr-himbaechel.exe).
# Output: build/top.fs
#Requires -Version 5.1
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
$Image    = 'hello-veryl-fpga'

if (-not (Get-Command veryl -ErrorAction SilentlyContinue)) {
    throw "veryl not found in PATH. See https://veryl-lang.org/"
}
if (-not (Get-Command podman -ErrorAction SilentlyContinue)) {
    throw "podman not found in PATH"
}

Push-Location $RepoRoot
try {
    New-Item -ItemType Directory -Force build | Out-Null

    veryl build
    if ($LASTEXITCODE -ne 0) { throw "veryl build failed" }

    # Veryl emits the source list with \\?\-prefixed absolute Windows paths.
    # Rewrite to repo-relative forward-slash paths so the same list works for
    # synth.ys on the host and inside the container (cwd = repo root).
    ((Get-Content hello_veryl.f) -replace '^\\\\\?\\', '') |
        ForEach-Object { $_.Replace("$RepoRoot\", '').Replace('\', '/') } |
        Set-Content -Encoding Ascii build\sources.f

    podman build -t $Image -f container\Containerfile container
    if ($LASTEXITCODE -ne 0) { throw "podman build failed" }

    podman run --rm -v "${RepoRoot}:/work" $Image bash scripts/synth_pnr.sh
    if ($LASTEXITCODE -ne 0) { throw "container synthesis failed" }
} finally {
    Pop-Location
}
