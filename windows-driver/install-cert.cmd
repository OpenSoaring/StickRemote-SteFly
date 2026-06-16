@echo off
REM ============================================================================
REM install-cert.cmd
REM
REM Importiert das SteFly-Code-Signing-Zertifikat als vertrauenswuerdig.
REM Muss EINMAL pro Ziel-PC laufen, BEVOR install-driver.cmd ausgefuehrt wird.
REM
REM Benoetigt: Administrator-Rechte
REM ============================================================================

setlocal
cd /d "%~dp0"

net session >NUL 2>&1
if errorlevel 1 (
    echo.
    echo FEHLER: Dieses Skript braucht Administrator-Rechte.
    echo Rechtsklick auf install-cert.cmd, dann "Als Administrator ausfuehren".
    echo.
    pause
    exit /b 1
)

set "CER=stefly-codesign.cer"

if not exist "%CER%" (
    echo FEHLER: %CER% nicht gefunden im aktuellen Verzeichnis.
    pause
    exit /b 2
)

echo SteFly Stick Remote - Zertifikat-Vertrauensstellung
echo ===================================================
echo.
echo Importiere %CER% in:
echo   - Vertrauenswuerdige Stammzertifizierungsstellen (Root)
echo   - Vertrauenswuerdige Herausgeber (TrustedPublisher)
echo.

certutil -addstore Root            "%CER%"
set "RC1=%ERRORLEVEL%"
certutil -addstore TrustedPublisher "%CER%"
set "RC2=%ERRORLEVEL%"

echo.
if "%RC1%"=="0" if "%RC2%"=="0" (
    echo Zertifikat erfolgreich vertrauenswuerdig gemacht.
    echo Jetzt kann install-driver.cmd ausgefuehrt werden.
) else (
    echo Mindestens ein Import-Schritt fehlgeschlagen.
    echo Root-Import:           ExitCode %RC1%
    echo TrustedPublisher-Import: ExitCode %RC2%
)

echo.
pause
endlocal
