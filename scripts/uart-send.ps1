# One-shot UART exchange: writes -Data to TX (LF appended unless -NoNewline),
# then prints RX until the line stays quiet for -QuietMs. No external packages.
#Requires -Version 5.1
param(
    [Parameter(Mandatory = $true)]
    [string]$Data,
    [string]$Port,
    [int]$BaudRate = 115200,
    [int]$QuietMs = 500,
    [switch]$NoNewline
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

try {
    if ($NoNewline) { $serial.Write($Data) } else { $serial.Write($Data + "`n") }

    $received = ''
    $lastRx = [Diagnostics.Stopwatch]::StartNew()
    while ($lastRx.ElapsedMilliseconds -lt $QuietMs) {
        if ($serial.BytesToRead -gt 0) {
            $received += $serial.ReadExisting()
            $lastRx.Restart()
        }
        Start-Sleep -Milliseconds 10
    }

    if ($received.Length -gt 0) {
        Write-Host $received
    } else {
        Write-Host "(no response within ${QuietMs}ms)"
    }
} finally {
    $serial.Close()
}
