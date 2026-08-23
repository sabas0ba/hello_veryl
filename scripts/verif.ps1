# Run the post-synthesis verification suite (verif/) inside the podman
# container defined by container/Containerfile. Part of the regression:
# run alongside `.\scripts\veryl.ps1 test` before committing RTL changes.
# Usage: .\scripts\verif.ps1
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

podman run --rm --network none -v "${RepoRoot}:/work" $Image bash verif/run.sh
exit $LASTEXITCODE
