# One-shot UART exchange: writes -Data to TX (LF appended unless -NoNewline),
# then prints RX until the line stays quiet for -QuietMs. No external packages.
#   -Hex : dump received bytes as hex (16 bytes/line) instead of decoded text
#          (ReadExisting decodes as ASCII and folds >0x7F into '?', so use
#           this to inspect raw bytes)
#Requires -Version 5.1
param(
    [Parameter(Mandatory = $true)]
    [string]$Data,
    [string]$Port,
    [int]$BaudRate = 115200,
    [int]$QuietMs = 500,
    [switch]$NoNewline,
    [switch]$Hex
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

    $rxBytes = New-Object System.Collections.Generic.List[byte]
    $lastRx = [Diagnostics.Stopwatch]::StartNew()
    while ($lastRx.ElapsedMilliseconds -lt $QuietMs) {
        while ($serial.BytesToRead -gt 0) {
            $rxBytes.Add([byte]$serial.ReadByte())
            $lastRx.Restart()
        }
        Start-Sleep -Milliseconds 10
    }

    if ($rxBytes.Count -eq 0) {
        Write-Host "(no response within ${QuietMs}ms)"
    } elseif ($Hex) {
        for ($i = 0; $i -lt $rxBytes.Count; $i += 16) {
            $chunk = $rxBytes[$i..([Math]::Min($i + 15, $rxBytes.Count - 1))]
            $hexStr = ($chunk | ForEach-Object { '{0:x2}' -f $_ }) -join ' '
            Write-Host ('{0,4}: {1}' -f $i, $hexStr)
        }
        Write-Host "($($rxBytes.Count) bytes)"
    } else {
        Write-Host ([Text.Encoding]::ASCII.GetString($rxBytes.ToArray()))
    }
} finally {
    $serial.Close()
}
