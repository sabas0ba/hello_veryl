# Container-based build: Veryl transpile and synthesis/PnR/pack all run in a
# podman container (Windows-native binaries such as veryl.exe and
# nextpnr-himbaechel.exe are blocked under Smart App Control).
# Output: build/top.fs
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

    # Veryl (run in the container, cwd /work) emits the source list with
    # /work/-prefixed absolute paths. Rewrite to repo-relative paths so the
    # same list works for synth.ys with cwd = repo root.
    ((Get-Content hello_veryl.f) -replace '^/work/', '') |
        Set-Content -Encoding Ascii build\sources.f

    podman run --rm --network none -v "${RepoRoot}:/work" $Image bash scripts/synth_pnr.sh
    if ($LASTEXITCODE -ne 0) { throw "container synthesis failed" }
} finally {
    Pop-Location
}
