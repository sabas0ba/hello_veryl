# 実機の RV32IM コアで riscv-tests を実行する (docs/riscv.md「実機での riscv-tests 実行」)
#
# 前提: TopRv を software/monitor.S の ROM でビルドして書き込んであること．
#   .\scripts\fpga-run.ps1 bash software/build.sh monitor
#   .\scripts\build-rv.ps1
#   .\scripts\flash.ps1 -Bitstream build\top_rv.fs
#
# テストは build/riscv_tests_hw/*.bin (verif/riscv/build_tests.sh hw の生成物) を使い，
# シミュレーションと同じ判定基準 (tohost == 1 で pass) で集計する．
#
# シミュレーション用 (build/riscv_tests) は 0x0 リンクのため実機では使えない．
# リンカが la を絶対アドレスの li へ緩和し，データ参照がロードアドレス
# 0x1000_0000 に追従しないため．
#Requires -Version 5.1
param(
    [string]$Port = 'COM4',
    [string]$Dir  = 'build\riscv_tests_hw',
    [int]$TimeoutMs = 10000
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
if (-not [System.IO.Path]::IsPathRooted($Dir)) { $Dir = Join-Path $RepoRoot $Dir }
$files = Get-ChildItem -Path $Dir -Filter *.bin | Sort-Object Name
if ($files.Count -eq 0) { throw "$Dir に .bin が無い (verif/riscv/build_tests.sh を先に実行する)" }

$sp = New-Object System.IO.Ports.SerialPort $Port, 115200, 'None', 8, 'One'
$sp.ReadTimeout  = $TimeoutMs
$sp.WriteTimeout = $TimeoutMs
$sp.Open()

function Read-Char {
    try { return [char]$sp.ReadByte() } catch { return $null }
}

# モニタはバナーと '>' を送ってから受信待ちに入る．書き込み直後は既に
# 送り終えているため，プロンプトは待たずに送信し，応答は Read-Result が
# 'R' まで読み飛ばして拾う．
function Read-Result {
    # "R" + 8 桁 hex + CRLF を読む
    $deadline = (Get-Date).AddMilliseconds($TimeoutMs)
    $seenR = $false
    $hex = ''
    while ((Get-Date) -lt $deadline) {
        $c = Read-Char
        if ($null -eq $c) { break }
        if (-not $seenR) {
            if ($c -eq 'R') { $seenR = $true }
            continue
        }
        if ($c -eq "`r" -or $c -eq "`n") {
            if ($hex.Length -ge 8) { return $hex }
            continue
        }
        $hex += $c
        if ($hex.Length -eq 8) { return $hex }
    }
    return $null
}

try {
    Start-Sleep -Milliseconds 200
    $sp.DiscardInBuffer()

    $pass = 0
    $fail = 0
    foreach ($f in $files) {
        $bytes = [System.IO.File]::ReadAllBytes($f.FullName)
        if ($bytes.Length % 4 -ne 0) { throw "$($f.Name): 4 バイト境界でない" }
        $words = $bytes.Length / 4

        $hdr = [BitConverter]::GetBytes([uint32]$words)
        $sp.Write($hdr, 0, 4)
        $sp.Write($bytes, 0, $bytes.Length)

        $res = Read-Result
        if ($null -eq $res) {
            Write-Output ("{0,-20} TIMEOUT" -f $f.BaseName)
            $fail++
        } elseif ($res -eq '00000001') {
            Write-Output ("{0,-20} pass" -f $f.BaseName)
            $pass++
        } else {
            Write-Output ("{0,-20} FAIL tohost={1} (失敗したサブテスト番号 = tohost>>1)" -f $f.BaseName, $res)
            $fail++
        }
    }
    Write-Output ""
    Write-Output ("riscv-tests (実機): {0} pass / {1} fail / {2} 本" -f $pass, $fail, $files.Count)
    if ($fail -ne 0) { exit 1 }
} finally {
    $sp.Close()
}
