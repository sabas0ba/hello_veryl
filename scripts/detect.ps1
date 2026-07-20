# Detect Tang Nano 9K via JTAG IDCODE scan (read-only, no bitstream access)
#Requires -Version 5.1
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
$Suite    = Join-Path $RepoRoot 'tools\oss-cad-suite'
if (-not (Test-Path (Join-Path $Suite 'bin\openFPGALoader.exe'))) {
    throw "toolchain not found. Run scripts\setup-toolchain.ps1 first"
}
$env:PATH = (Join-Path $Suite 'bin') + ';' + (Join-Path $Suite 'lib') + ';' + $env:PATH

openFPGALoader --detect -b tangnano9k
if ($LASTEXITCODE -ne 0) { throw "openFPGALoader failed" }
