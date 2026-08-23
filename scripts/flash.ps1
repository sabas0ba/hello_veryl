# Write bitstream to Tang Nano 9K via openFPGALoader
#   default     : load to SRAM (volatile, lost on power cycle)
#   -Flash      : program embedded flash (persistent)
#   -Bitstream  : path to the .fs (default: build\top.fs)
#
# 他の Gowin ボード (Tang Primer 20K 等) が同時に接続されている場合の誤書き込みを
# 防ぐため，書き込み前に JTAG の IDCODE を確認し GW1N(R)-9C 以外なら中断する．
#Requires -Version 5.1
param(
    [switch]$Flash,
    [string]$Bitstream = 'build\top.fs'
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
$Suite    = Join-Path $RepoRoot 'tools\oss-cad-suite'
if (-not (Test-Path (Join-Path $Suite 'bin\openFPGALoader.exe'))) {
    throw "toolchain not found. Run scripts\setup-toolchain.ps1 first"
}
$env:PATH = (Join-Path $Suite 'bin') + ';' + (Join-Path $Suite 'lib') + ';' + $env:PATH

if (-not [System.IO.Path]::IsPathRooted($Bitstream)) {
    $Bitstream = Join-Path $RepoRoot $Bitstream
}
if (-not (Test-Path $Bitstream)) { throw "$Bitstream not found. Run the build first" }

# 対象デバイスの確認 (read-only)
$detect = (openFPGALoader --detect -b tangnano9k | Out-String)
if ($LASTEXITCODE -ne 0) { throw "openFPGALoader --detect failed" }
if ($detect -notmatch 'GW1N\(R\)-9C') {
    Write-Output $detect
    throw "Tang Nano 9K (GW1N(R)-9C) が見つからない．誤書き込みを防ぐため中断する"
}
Write-Output "target: GW1N(R)-9C (Tang Nano 9K)"
Write-Output "bitstream: $Bitstream"

if ($Flash) {
    openFPGALoader -b tangnano9k -f $Bitstream
} else {
    openFPGALoader -b tangnano9k $Bitstream
}
if ($LASTEXITCODE -ne 0) { throw "openFPGALoader failed" }
