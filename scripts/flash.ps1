# Write bitstream to Tang Nano 9K via openFPGALoader
#   default     : load to SRAM (volatile, lost on power cycle)
#   -Flash      : program embedded flash (persistent)
#   -Bitstream  : path to the .fs (default: build\top.fs)
#
# 他の Gowin ボード (Tang Primer 20K 等) が同時に接続されていると，どちらも
# FTDI 0403:6010 として列挙されるため対象を取り違えうる．USB の bus/dev 番号は
# 抜き差しや再列挙で変わり，シリアル文字列も一致しないことがあるため，
# **IDCODE が GW1N(R)-9C のデバイスを探して**書き込む．見つからなければ中断する．
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

$Target = 'GW1N(R)-9C'

function Test-Target([string[]]$extra) {
    # PowerShell 5.1 ではネイティブコマンドの stderr が ErrorRecord に包まれ，
    # ErrorActionPreference=Stop のままだと例外になる．関数スコープで抑止する
    $ErrorActionPreference = 'SilentlyContinue'
    $cliArgs = @('--detect', '-b', 'tangnano9k') + $extra
    $out     = (& openFPGALoader @cliArgs 2>&1 | Out-String)
    return ($out -match [regex]::Escape($Target))
}

# 1. 既定の選択で当たるか
$sel = @()
$found = Test-Target $sel

# 2. 外れたら bus/dev を総当りする (列挙は抜き差しで変わる)
if (-not $found) {
    Write-Output "既定の選択では $Target が見つからないため bus/dev を探索する"
    foreach ($bus in 1..2) {
        foreach ($dev in 1..40) {
            $try = @('--busdev-num', "${bus}:${dev}")
            if (Test-Target $try) {
                $sel   = $try
                $found = $true
                Write-Output "found at bus/dev ${bus}:${dev}"
                break
            }
        }
        if ($found) { break }
    }
}

if (-not $found) {
    throw "$Target が見つからない．Tang Nano 9K の接続を確認する (他ボードのみ接続されている / USB を掴んだプロセスが残っている 等)．誤書き込みを防ぐため中断する"
}

Write-Output "target: $Target (Tang Nano 9K)"
Write-Output "bitstream: $Bitstream"

if ($Flash) {
    & openFPGALoader -b tangnano9k @sel -f $Bitstream
} else {
    & openFPGALoader -b tangnano9k @sel $Bitstream
}
if ($LASTEXITCODE -ne 0) { throw "openFPGALoader failed" }
