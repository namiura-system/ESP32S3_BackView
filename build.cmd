@echo off
rem ESP-IDF v6.1 (EIM) environment -> idf.py build
cd /d %~dp0
pwsh -NoProfile -ExecutionPolicy Bypass -Command ". 'C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1' *> $null; idf.py build; exit $LASTEXITCODE"
pause
