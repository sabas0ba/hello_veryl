# Interactive UART terminal: prints RX, sends typed keys to TX. Exit with Esc.
#   -Hex      : dump received bytes as hex (16 bytes/line) instead of text
#   -MaxBytes : exit automatically after receiving N bytes (0 = unlimited)
# Uses the on-board UART (FTDI-compatible Interface 1). No external packages.
#Requires -Version 5.1
param(
    [string]$Port,
    [int]$BaudRate = 115200,
    [switch]$Hex,
    [int]$MaxBytes = 0
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not $Port) {
    # Auto-detect the FTDI-compatible COM port (Tang Nano 9K UART)
    $names = Get-CimInstance Win32_PnPEntity -Filter "PNPDeviceID LIKE 'FTDIBUS%'" |
        Select-Object -ExpandProperty Name
    foreach ($n in $names) {
        if ($n -match '\((COM\d+)\)') { $Port = $Matches[1]; break }
    }
    if (-not $Port) { throw "FTDI COM port not found. Specify -Port explicitly" }
}

$serial = New-Object System.IO.Ports.SerialPort $Port, $BaudRate, 'None', 8, 'One'
$serial.Open()
$serial.DiscardInBuffer()
Write-Host "Connected: $Port @ $BaudRate baud (8N1). Press Esc to exit."

# Keyboard polling fails when no interactive console is attached (e.g. capture
# runs with redirected stdin); disable it on first failure
$keyboardOk = $true
$rxCount    = 0
$done       = $false

try {
    while (-not $done) {
        while ($serial.BytesToRead -gt 0 -and -not $done) {
            if ($Hex) {
                $b = $serial.ReadByte()
                Write-Host -NoNewline ('{0:x2} ' -f $b)
                $rxCount++
                if ($rxCount % 16 -eq 0) { Write-Host '' }
            } else {
                $s = $serial.ReadExisting()
                Write-Host -NoNewline $s
                $rxCount += $s.Length
            }
            if ($MaxBytes -gt 0 -and $rxCount -ge $MaxBytes) { $done = $true }
        }
        if (-not $done -and $keyboardOk) {
            try {
                if ([Console]::KeyAvailable) {
                    $key = [Console]::ReadKey($true)
                    if ($key.Key -eq 'Escape') { $done = $true }
                    else { $serial.Write($key.KeyChar) }
                }
            } catch {
                $keyboardOk = $false
            }
        }
        Start-Sleep -Milliseconds 10
    }
} finally {
    $serial.Close()
    Write-Host "`nDisconnected: $Port ($rxCount bytes received)"
}
