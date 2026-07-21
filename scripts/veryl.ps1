# Run veryl inside the podman container defined by container/Containerfile
# (Windows-native veryl.exe is blocked under Smart App Control).
# Usage: .\scripts\veryl.ps1 <veryl args...>   e.g. .\scripts\veryl.ps1 test
#Requires -Version 5.1
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
$Image    = 'hello-veryl-fpga'

if (-not (Get-Command podman -ErrorAction SilentlyContinue)) {
    throw "podman not found in PATH"
}

# Cached after the first run; keeps the image in sync with the Containerfile
podman build -t $Image -f "$RepoRoot\container\Containerfile" "$RepoRoot\container"
if ($LASTEXITCODE -ne 0) { throw "podman build failed" }

# --network none: the project has no registry dependencies, so veryl needs no
# network at runtime (remove if [dependencies] are added to Veryl.toml)
podman run --rm --network none -v "${RepoRoot}:/work" $Image veryl @args
exit $LASTEXITCODE
