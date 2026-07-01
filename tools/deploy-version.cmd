@echo off
cd /d "%~dp0\.."
py -3 tools\deploy-version.py
pause