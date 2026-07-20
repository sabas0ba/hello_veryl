# Write bitstream to Tang Nano 9K via openFPGALoader
#   default : load to SRAM (volatile, lost on power cycle)
#   -Flash  : program embedded flash (persistent)
#Requires -Version 5.1
param(
    [switch]$Flash
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
$Suite    = Join-Path $RepoRoot 'tools\oss-cad-suite'
if (-not (Test-Path (Join-Path $Suite 'bin\openFPGALoader.exe'))) {
    throw "toolchain not found. Run scripts\setup-toolchain.ps1 first"
}
$env:PATH = (Join-Path $Suite 'bin') + ';' + (Join-Path $Suite 'lib') + ';' + $env:PATH

$Bitstream = Join-Path $RepoRoot 'build\top.fs'
if (-not (Test-Path $Bitstream)) { throw "build\top.fs not found. Run scripts\build.ps1 first" }

if ($Flash) {
    openFPGALoader -b tangnano9k -f $Bitstream
} else {
    openFPGALoader -b tangnano9k $Bitstream
}
if ($LASTEXITCODE -ne 0) { throw "openFPGALoader failed" }
