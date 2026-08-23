# UART ブートモニタへプログラムを流し込んで走らせる (docs/riscv.md「LCD デモ」)
#
# 前提: TopRv を software/monitor.S の ROM でビルドして書き込んであること．
#   .\scripts\fpga-run.ps1 bash software/build.sh monitor
#   .\scripts\build-rv.ps1
#   .\scripts\flash.ps1 -Bitstream build\top_rv.fs
#
# 使い方:
#   .\scripts\fpga-run.ps1 bash software/build_demo.sh torus
#   .\scripts\rv-load.ps1 -Bin build\software\torus.bin -Seconds 10
#
# 送信後は -Seconds のあいだ受信を数え，スループットとフレームレートを出す．
# 走り続けるプログラム (torus) はスクリプト終了後もボード上で動き続ける．
#Requires -Version 5.1
param(
    [string]$Bin = 'build\software\torus.bin',
    [string]$Port = 'COM4',
    [int]$Seconds = 10,
    # 1 フレームの送信バイト数 (torus: FF + 100x29)
    [int]$FrameBytes = 2901,
    # 先頭の受信内容をこの行数ぶん表示する (0 で表示しない)
    [int]$ShowLines = 0
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
if (-not [System.IO.Path]::IsPathRooted($Bin)) { $Bin = Join-Path $RepoRoot $Bin }
if (-not (Test-Path $Bin)) { throw "$Bin が無い (software/build_demo.sh を先に実行する)" }

$bytes = [System.IO.File]::ReadAllBytes($Bin)
# モニタは 4 バイト単位で受け取るため，端数はゼロで埋める
$pad = (4 - ($bytes.Length % 4)) % 4
if ($pad -ne 0) {
    $padded = New-Object byte[] ($bytes.Length + $pad)
    [Array]::Copy($bytes, $padded, $bytes.Length)
    $bytes = $padded
    Write-Output ("note: {0} byte をゼロで詰めて 4 バイト境界に合わせた" -f $pad)
}
$words = $bytes.Length / 4

$sp = New-Object System.IO.Ports.SerialPort $Port, 115200, 'None', 8, 'One'
$sp.ReadTimeout  = 1000
$sp.WriteTimeout = 5000
$sp.Open()
try {
    Start-Sleep -Milliseconds 200
    $sp.DiscardInBuffer()

    Write-Output ("sending {0} words ({1} byte) from {2}" -f $words, $bytes.Length, $Bin)
    $hdr = [BitConverter]::GetBytes([uint32]$words)
    $sp.Write($hdr, 0, 4)
    $sp.Write($bytes, 0, $bytes.Length)

    if ($Seconds -le 0) {
        Write-Output 'sent. (受信は数えない)'
        return
    }

    # fps を測るときは，描きかけの最初のフレームを避けるため少し待って捨てる．
    # 短い出力の診断プログラムでは捨てると何も残らないので，そのまま数える．
    if ($FrameBytes -gt 0) {
        Start-Sleep -Milliseconds 500
        $sp.DiscardInBuffer()
    }

    $buf = New-Object byte[] 4096
    $total = 0
    $head = New-Object System.Text.StringBuilder
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt $Seconds) {
        try { $n = $sp.Read($buf, 0, $buf.Length) } catch { $n = 0 }
        if ($n -gt 0) {
            $total += $n
            if ($ShowLines -gt 0 -and $head.Length -lt ($ShowLines * 101)) {
                for ($i = 0; $i -lt $n; $i++) {
                    $c = [int]$buf[$i]
                    # FF (フレーム先頭) と LF を改行に寄せ，CR は捨てる
                    if ($c -eq 12 -or $c -eq 10) { [void]$head.Append("`n") }
                    elseif ($c -eq 13) { }
                    elseif ($c -ge 32 -and $c -le 126) { [void]$head.Append([char]$c) }
                }
            }
        }
    }
    $sw.Stop()

    $sec = $sw.Elapsed.TotalSeconds
    $bps = $total / $sec
    Write-Output ''
    Write-Output ("received {0} byte in {1:F1} s = {2:F0} byte/s" -f $total, $sec, $bps)
    if ($FrameBytes -gt 0) {
        Write-Output ("frame rate: {0:F2} fps ({1} byte/frame)" -f ($bps / $FrameBytes), $FrameBytes)
    }
    if ($ShowLines -gt 0) {
        Write-Output ''
        # 100 桁を超える行 (torus の 1 フレーム) は折り返して出す
        foreach ($line in $head.ToString() -split "`n") {
            if ($line.Length -le 100) {
                Write-Output $line
            } else {
                for ($i = 0; $i -lt $line.Length; $i += 100) {
                    $len = [Math]::Min(100, $line.Length - $i)
                    Write-Output $line.Substring($i, $len)
                }
            }
        }
    }
}
finally {
    $sp.Close()
}
