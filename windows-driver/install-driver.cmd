@echo off
REM ============================================================================
REM install-driver.cmd
REM
REM Installiert die SteFly-Stick-Remote-INF systemweit in den Windows-
REM Treiber-Store. Danach erkennt JEDER User auf diesem Rechner den Stick
REM unter dem richtigen Namen, auch ohne vorherige manuelle Treiberauswahl.
REM
REM Benoetigt: Administrator-Rechte (Rechtsklick "Als Administrator ausfuehren")
REM Windows 10 / 11
REM ============================================================================

setlocal
cd /d "%~dp0"

REM Pruefen, ob als Administrator
net session >NUL 2>&1
if errorlevel 1 (
    echo.
    echo FEHLER: Dieses Skript braucht Administrator-Rechte.
    echo Rechtsklick auf install-driver.cmd, dann "Als Administrator ausfuehren".
    echo.
    pause
    exit /b 1
)

echo SteFly Stick Remote Treiber-Installation
echo ========================================
echo.

set "INF=stefly-stick-remote.inf"

if not exist "%INF%" (
    echo FEHLER: %INF% nicht gefunden im aktuellen Verzeichnis.
    pause
    exit /b 2
)

echo Installiere %INF% in den Windows-Treiber-Store ...
echo.

pnputil /add-driver "%INF%" /install
set "RC=%ERRORLEVEL%"

echo.
if "%RC%"=="0" (
    echo Installation erfolgreich.
    echo.
    echo Wenn der Stick angeschlossen ist, bitte einmal aus- und wieder
    echo einstecken, damit Windows den neuen Treibernamen anwendet.
) else if "%RC%"=="3010" (
    echo Installation erfolgreich, ein Neustart wird empfohlen.
) else (
    echo Installation fehlgeschlagen, Exit-Code %RC%.
    echo Pruefe die Ausgabe oben.
)

echo.
pause
endlocal
exit /b %RC%
