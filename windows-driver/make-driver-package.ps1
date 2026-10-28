<#
.SYNOPSIS
  Erzeugt das Auslieferungs-ZIP fuer den Treiber von SteFly Stick Remote
  und SteFly Rotary Panel (eine INF fuer beide Geraete).

.DESCRIPTION
  Bundelt INF, signierte CAT, Public-Cert, beide Installer und eine
  LIESMICH.txt in ein ZIP. Die PFX (private key) wird BEWUSST nicht
  mitgepackt - die gehoert ausschliesslich auf dein Entwicklungssystem.

.PARAMETER OutputPath
  Pfad fuer das fertige ZIP. Default: SteFly-Stick-Remote-Treiber.zip
  im Projektwurzel-Ordner.

.PARAMETER Force
  Vorhandenes ZIP ueberschreiben.

.EXAMPLE
  .\make-driver-package.ps1
  Erzeugt das Standard-ZIP im Projektwurzel-Ordner.

.EXAMPLE
  .\make-driver-package.ps1 -OutputPath C:\SteFly-Lieferungen\v1.0\driver.zip -Force
#>
[CmdletBinding()]
param(
    [string]$OutputPath = $null,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

$here        = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = Split-Path -Parent $here

if (-not $OutputPath) {
    $OutputPath = Join-Path $projectRoot "SteFly-Stick-Remote-Treiber.zip"
}

# --- 1) Pflicht-Dateien pruefen ---------------------------------------------
$required = @(
    "stefly-stick-remote.inf",
    "stefly-stick-remote.cat",
    "stefly-codesign.cer",
    "install-cert.cmd",
    "install-driver.cmd"
)

$missing = @()
foreach ($f in $required) {
    $full = Join-Path $here $f
    if (-not (Test-Path $full)) {
        $missing += $f
    }
}

if ($missing.Count -gt 0) {
    Write-Host "Folgende Pflicht-Dateien fehlen:" -ForegroundColor Red
    $missing | ForEach-Object { Write-Host "  - $_" }
    Write-Host ""
    Write-Host "Vermutlich erst .\sign-driver.ps1 ausfuehren, um cat und cer zu erzeugen." -ForegroundColor Yellow
    exit 1
}

# --- 2) LIESMICH.txt (Deutsch) frisch erzeugen ------------------------------
$liesmich = Join-Path $here "LIESMICH.txt"
$liesmichContent = @"
SteFly Stick Remote / Rotary Panel - Treiber-Installation
========================================================

Diese Treiber-Datei sorgt dafuer, dass Windows den SteFly Stick Remote
und das SteFly Rotary Panel mit dem richtigen Namen im Geraete-Manager
anzeigt ("SteFly Stick Remote" bzw. "SteFly Rotary Panel" statt
"Serielles USB-Geraet"). Der eigentliche Treiber ist Windows-eigener
usbser.sys - es wird kein fremder Kernel-Treiber installiert.

Installation (einmalig pro PC):
-------------------------------

  1) install-cert.cmd
       Rechtsklick -> "Als Administrator ausfuehren"
       Bestaetigt das SteFly-Signaturzertifikat als vertrauenswuerdig.

  2) install-driver.cmd
       Rechtsklick -> "Als Administrator ausfuehren"
       Installiert die INF systemweit in den Treiber-Store.

  3) Stick bzw. Panel einstecken (oder einmal abziehen + neu einstecken,
     falls es schon angeschlossen war).

  4) Im Geraete-Manager steht das Geraet jetzt als "SteFly Stick Remote (COMx)"
     bzw. "SteFly Rotary Panel (COMx)" unter "Anschluesse (COM und LPT)".

Bei Problemen
-------------

  * "Als Administrator ausfuehren" zwingend - Standard-Doppelklick reicht
    NICHT, weil der Treiber-Store systemweite Rechte braucht.

  * Wenn install-driver.cmd "INF nicht signiert" meldet: install-cert.cmd
    wurde nicht vorher ausgefuehrt oder hat nicht beide Stores
    (Root + TrustedPublisher) erfolgreich befuellt.

  * Stick und Panel funktionieren auch OHNE diese Treiber-Installation -
    Windows nutzt dann den generischen Namen "Serielles USB-Geraet (COMx)",
    die HID-Tastatur und -Maus laufen aber normal.

Support
-------

SteFly.aero - Kontakt: siehe https://www.stefly.aero
"@

Set-Content -Path $liesmich -Value $liesmichContent -Encoding UTF8
Write-Host "LIESMICH.txt erzeugt: $liesmich" -ForegroundColor Green

# --- 2b) README.txt (English) -----------------------------------------------
$readme = Join-Path $here "README.txt"
$readmeContent = @"
SteFly Stick Remote / Rotary Panel - Driver Installation
========================================================

This driver package makes Windows display the SteFly Stick Remote and the
SteFly Rotary Panel with the correct name in Device Manager ("SteFly Stick
Remote" or "SteFly Rotary Panel" instead of "USB Serial Device"). The actual
driver used is the Windows built-in usbser.sys - no third-party kernel driver
is installed.

Installation (once per PC):
---------------------------

  1) install-cert.cmd
       Right-click -> "Run as administrator"
       Adds the SteFly signing certificate to the trusted publishers list.

  2) install-driver.cmd
       Right-click -> "Run as administrator"
       Installs the INF system-wide into the Windows driver store.

  3) Plug in the stick or panel (or unplug and replug it if it was already
     connected).

  4) Device Manager will now show it as "SteFly Stick Remote (COMx)" or
     "SteFly Rotary Panel (COMx)" under "Ports (COM & LPT)".

Troubleshooting
---------------

  * "Run as administrator" is mandatory - a regular double-click is NOT enough
    because the Windows driver store requires elevated permissions.

  * If install-driver.cmd reports "INF not signed": install-cert.cmd was not
    executed first, or did not successfully populate both stores (Root and
    TrustedPublisher).

  * Stick and panel work WITHOUT this driver installation as well - Windows will
    just use the generic name "USB Serial Device (COMx)", but the HID keyboard
    and mouse functions will operate normally.

Support
-------

SteFly.aero - contact details at https://www.stefly.aero
"@

Set-Content -Path $readme -Value $readmeContent -Encoding UTF8
Write-Host "README.txt erzeugt:   $readme" -ForegroundColor Green

# --- 3) Sicherheitscheck: niemals .pfx mit reinpacken -----------------------
if (Test-Path (Join-Path $here "stefly-codesign.pfx")) {
    Write-Host ""
    Write-Host "WARNUNG: stefly-codesign.pfx (privater Schluessel) ist vorhanden." -ForegroundColor Yellow
    Write-Host "         Wird BEWUSST NICHT mitgepackt - bleibt nur auf diesem PC." -ForegroundColor Yellow
}

# --- 4) Zip bauen -----------------------------------------------------------
if (Test-Path $OutputPath) {
    if ($Force) {
        Remove-Item $OutputPath -Force
    } else {
        Write-Error "ZIP existiert schon: $OutputPath  (mit -Force ueberschreiben)"
        exit 2
    }
}

$filesToZip = $required + @("LIESMICH.txt", "README.txt") | ForEach-Object { Join-Path $here $_ }

Compress-Archive -Path $filesToZip -DestinationPath $OutputPath -CompressionLevel Optimal
Write-Host ""
Write-Host "Auslieferungs-ZIP erzeugt:" -ForegroundColor Green
Write-Host "  $OutputPath"
$zipSize = (Get-Item $OutputPath).Length
Write-Host "  Groesse: $([math]::Round($zipSize/1KB, 1)) KB"
Write-Host ""
Write-Host "Inhalt:" -ForegroundColor Cyan
Get-Item $filesToZip | Select-Object Name, Length | Format-Table
