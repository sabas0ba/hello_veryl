# Run an arbitrary command inside the podman container defined by
# container/Containerfile (same relay pattern as veryl.ps1, but not limited
# to the veryl subcommands). cwd inside the container is /work (= repo root).
# Usage: .\scripts\fpga-run.ps1 <command> [args...]
#   e.g. .\scripts\fpga-run.ps1 yosys -s scripts/synth.ys
#        .\scripts\fpga-run.ps1 bash verif/run.sh
#        .\scripts\fpga-run.ps1 /opt/oss-cad-suite/py3bin/python3 <script.py>
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

podman run --rm --network none -v "${RepoRoot}:/work" $Image @args
exit $LASTEXITCODE
