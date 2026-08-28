# Start a TF writer from the UART boot monitor and send a file via XMODEM-CRC.
#
# The first four payload bytes contain the little-endian file size. This
# application framing distinguishes the file tail from XMODEM padding.
#Requires -Version 5.1
param(
    [string]$Bin = 'build\software\tfdump.bin',
    [string]$Receiver = 'build\software\tfwrite.bin',
    [string]$Port = 'COM4',
    [int]$BaudRate = 115200,
    [int]$ResultTimeoutSeconds = 60,
    # Drop SOH only on the first attempt for this block, then require NAK.
    [int]$DropSohOnceAtBlock = 0,
    [switch]$SkipReceiver
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$SOH = 0x01
$EOT = 0x04
$ACK = 0x06
$NAK = 0x15
$CAN = 0x18
$CRCRequest = 0x43
$BlockSize = 128
$MaxRetries = 16
$MaxPayload = 0x00400000

$RepoRoot = Split-Path -Parent $PSScriptRoot
if (-not [System.IO.Path]::IsPathRooted($Bin)) {
    $Bin = Join-Path $RepoRoot $Bin
}
if (-not [System.IO.Path]::IsPathRooted($Receiver)) {
    $Receiver = Join-Path $RepoRoot $Receiver
}
if (-not (Test-Path -LiteralPath $Bin)) {
    throw "$Bin does not exist"
}
if (-not $SkipReceiver -and -not (Test-Path -LiteralPath $Receiver)) {
    throw "$Receiver does not exist (build a TF writer with software/build_demo.sh <name> ram)"
}

function Get-Crc16([byte[]]$Data) {
    [int]$crc = 0
    foreach ($b in $Data) {
        $crc = $crc -bxor ([int]$b -shl 8)
        foreach ($unused in 0..7) {
            if (($crc -band 0x8000) -ne 0) {
                $crc = (($crc -shl 1) -bxor 0x1021) -band 0xffff
            } else {
                $crc = ($crc -shl 1) -band 0xffff
            }
        }
    }
    return $crc
}

function Wait-Byte(
    [System.IO.Ports.SerialPort]$Serial,
    [int[]]$Expected,
    [int]$TimeoutSeconds,
    [System.Text.StringBuilder]$Text
) {
    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    while ($timer.Elapsed.TotalSeconds -lt $TimeoutSeconds) {
        try {
            $value = $Serial.ReadByte()
        } catch [System.TimeoutException] {
            continue
        }
        if ($Expected -contains $value) {
            return $value
        }
        if ($null -ne $Text) {
            if ($value -eq 10) {
                [void]$Text.Append("`n")
            } elseif ($value -ne 13 -and $value -ge 32 -and $value -le 126) {
                [void]$Text.Append([char]$value)
            }
        }
    }
    return -1
}

function Send-MonitorImage(
    [System.IO.Ports.SerialPort]$Serial,
    [byte[]]$Image
) {
    $pad = (4 - ($Image.Length % 4)) % 4
    if ($pad -ne 0) {
        $padded = New-Object byte[] ($Image.Length + $pad)
        [Array]::Copy($Image, $padded, $Image.Length)
        $Image = $padded
    }
    [byte[]]$header = [BitConverter]::GetBytes([uint32]($Image.Length / 4))
    $Serial.Write($header, 0, $header.Length)
    $Serial.Write($Image, 0, $Image.Length)
}

function Send-XmodemBlock(
    [System.IO.Ports.SerialPort]$Serial,
    [int]$Number,
    [byte[]]$Data,
    [switch]$DropSohOnce
) {
    [byte[]]$packet = New-Object byte[] (3 + $BlockSize + 2)
    $packet[0] = $SOH
    $packet[1] = [byte]($Number -band 0xff)
    $packet[2] = [byte](0xff -bxor $packet[1])
    [Array]::Copy($Data, 0, $packet, 3, $BlockSize)
    $crc = Get-Crc16 $Data
    $packet[3 + $BlockSize] = [byte](($crc -shr 8) -band 0xff)
    $packet[4 + $BlockSize] = [byte]($crc -band 0xff)

    foreach ($attempt in 1..$MaxRetries) {
        $injected = $DropSohOnce -and $attempt -eq 1
        if ($injected) {
            Write-Output "injecting lost SOH at block $Number"
            $Serial.Write($packet, 1, $packet.Length - 1)
        } else {
            $Serial.Write($packet, 0, $packet.Length)
        }
        $response = Wait-Byte $Serial @($ACK, $NAK, $CAN) 3 $null
        if ($injected) {
            if ($response -ne $NAK) {
                throw "lost-SOH injection at block $Number did not receive NAK"
            }
            Write-Output "  block ${Number}: NAK received, retransmitting"
            continue
        }
        if ($response -eq $ACK) {
            return
        }
        if ($response -eq $CAN) {
            throw "receiver cancelled block $Number"
        }
    }
    throw "no ACK for block $Number"
}

[byte[]]$file = [System.IO.File]::ReadAllBytes($Bin)
if ($file.Length -eq 0 -or $file.Length + 4 -gt $MaxPayload) {
    throw "file size $($file.Length) is outside 1..$($MaxPayload - 4) bytes"
}
[byte[]]$payload = New-Object byte[] ($file.Length + 4)
[byte[]]$lengthBytes = [BitConverter]::GetBytes([uint32]$file.Length)
[Array]::Copy($lengthBytes, 0, $payload, 0, 4)
[Array]::Copy($file, 0, $payload, 4, $file.Length)
$blockCount = [int][Math]::Ceiling($payload.Length / [double]$BlockSize)
if ($DropSohOnceAtBlock -lt 0 -or $DropSohOnceAtBlock -gt $blockCount) {
    throw "DropSohOnceAtBlock must be between 0 and $blockCount"
}

$serial = New-Object System.IO.Ports.SerialPort $Port, $BaudRate, 'None', 8, 'One'
$serial.ReadTimeout = 100
$serial.WriteTimeout = 5000
$serial.Open()
$transferStarted = $false
try {
    if (-not $SkipReceiver) {
        $monitorText = New-Object System.Text.StringBuilder
        $prompt = Wait-Byte $serial @([int][char]'>') 10 $monitorText
        if ($prompt -ne [int][char]'>') {
            # The monitor emits its prompt only once at reset.  If the host opens
            # the FTDI UART afterwards, that byte may already have been dropped,
            # while the monitor is still correctly waiting for the image header.
            Write-Output 'boot monitor prompt was not captured; probing image receiver'
        }
        [byte[]]$receiverImage = [System.IO.File]::ReadAllBytes($Receiver)
        Write-Output ("starting receiver: {0} ({1} byte)" -f $Receiver, $receiverImage.Length)
        Send-MonitorImage $serial $receiverImage
    }

    $startupText = New-Object System.Text.StringBuilder
    $request = Wait-Byte $serial @($CRCRequest) 15 $startupText
    if ($request -ne $CRCRequest) {
        throw "receiver CRC request ('C') not received: $($startupText.ToString())"
    }
    if ($startupText.Length -gt 0) {
        Write-Output $startupText.ToString().Trim()
    }

    Write-Output ("sending {0} byte as {1} XMODEM blocks" -f $file.Length, $blockCount)
    $transferStarted = $true
    for ($blockIndex = 0; $blockIndex -lt $blockCount; $blockIndex++) {
        [byte[]]$block = New-Object byte[] $BlockSize
        for ($j = 0; $j -lt $block.Length; $j++) {
            $block[$j] = 0x1a
        }
        $offset = $blockIndex * $BlockSize
        $count = [Math]::Min($BlockSize, $payload.Length - $offset)
        [Array]::Copy($payload, $offset, $block, 0, $count)
        $dropSoh = $blockIndex + 1 -eq $DropSohOnceAtBlock
        Send-XmodemBlock $serial ($blockIndex + 1) $block -DropSohOnce:$dropSoh
        if ((($blockIndex + 1) % 32) -eq 0 -or $blockIndex + 1 -eq $blockCount) {
            Write-Output ("  blocks {0}/{1}" -f ($blockIndex + 1), $blockCount)
        }
    }

    $eotAcked = $false
    foreach ($attempt in 1..$MaxRetries) {
        $serial.Write([byte[]]@($EOT), 0, 1)
        $response = Wait-Byte $serial @($ACK, $NAK, $CAN) 3 $null
        if ($response -eq $ACK) {
            $eotAcked = $true
            break
        }
        if ($response -eq $CAN) {
            throw 'receiver cancelled at EOT'
        }
    }
    if (-not $eotAcked) {
        throw 'no ACK for EOT'
    }

    $resultText = New-Object System.Text.StringBuilder
    $prompt = Wait-Byte $serial @([int][char]'>') $ResultTimeoutSeconds $resultText
    $result = $resultText.ToString()
    if ($result.Length -gt 0) {
        Write-Output $result.Trim()
    }
    if ($prompt -ne [int][char]'>') {
        throw 'boot monitor prompt not received after TF writer'
    }
    if ($result -notmatch 'R([0-9A-Fa-f]{8})' -or $Matches[1] -ne '00000000') {
        throw "TF writer failed: $result"
    }
    Write-Output 'XMODEM file write completed'
}
catch {
    if ($transferStarted -and $serial.IsOpen) {
        try {
            $serial.Write([byte[]]@($CAN, $CAN), 0, 2)
        } catch {
        }
    }
    throw
}
finally {
    $serial.Close()
}
