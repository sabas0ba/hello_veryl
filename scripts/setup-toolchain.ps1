# OSS CAD Suite (yosys / nextpnr-himbaechel / apicula / openFPGALoader) setup
# - Pinned release + SHA-256 verification (digest cross-checked with GitHub Releases API)
# - Trust anchor: archive and digest share the same origin (GitHub release) and
#   upstream provides no signature, so a compromised release itself is not
#   detectable. This pin detects transport tampering and post-pin modification.
# - Installs into <repo>/tools/oss-cad-suite (git-ignored); removal = delete the directory
#Requires -Version 5.1
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ReleaseTag = '2026-07-20'
$FileName   = 'oss-cad-suite-windows-x64-20260720.exe'
$Url        = "https://github.com/YosysHQ/oss-cad-suite-build/releases/download/$ReleaseTag/$FileName"
$Sha256     = '03AB812DCD2E094148BC2009CA7BA7358C805A0D848FE46DF14D9CB4BFED5893'

$RepoRoot   = Split-Path -Parent $PSScriptRoot
$ToolsDir   = Join-Path $RepoRoot 'tools'
$Installer  = Join-Path $ToolsDir $FileName
$InstallDir = Join-Path $ToolsDir 'oss-cad-suite'

if (Test-Path (Join-Path $InstallDir 'bin\yosys.exe')) {
    Write-Host "Already installed: $InstallDir"
    exit 0
}

New-Item -ItemType Directory -Force $ToolsDir | Out-Null

if (-not (Test-Path $Installer)) {
    Write-Host "Downloading $Url"
    & curl.exe --fail --location --proto '=https' --tlsv1.2 --output $Installer $Url
    if ($LASTEXITCODE -ne 0) { throw "download failed (curl exit $LASTEXITCODE)" }
}

$actual = (Get-FileHash -Algorithm SHA256 $Installer).Hash
if ($actual -ne $Sha256) {
    Remove-Item $Installer -Confirm:$false
    throw "SHA-256 mismatch: expected $Sha256, got $actual (installer deleted)"
}
Write-Host "SHA-256 verified: $actual"

# 7z self-extracting archive: prefer extraction via an installed 7z (no
# execution of the downloaded binary); otherwise run the SFX via
# Start-Process for a reliable wait and exit code
$SevenZip = Get-Command 7z -ErrorAction SilentlyContinue
if ($SevenZip) {
    & $SevenZip.Source x $Installer "-o$ToolsDir" -y
    if ($LASTEXITCODE -ne 0) { throw "7z extraction failed (exit $LASTEXITCODE)" }
} else {
    $proc = Start-Process -FilePath $Installer -WorkingDirectory $ToolsDir -Wait -NoNewWindow -PassThru
    if ($proc.ExitCode -ne 0) { throw "extraction failed (exit $($proc.ExitCode))" }
}

if (-not (Test-Path (Join-Path $InstallDir 'bin\yosys.exe'))) {
    throw "extraction finished but bin\yosys.exe not found under $InstallDir"
}
Write-Host "Installed: $InstallDir (release $ReleaseTag)"
