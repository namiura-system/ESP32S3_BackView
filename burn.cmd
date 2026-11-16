@echo off
rem ESP-IDF v6.1 (EIM) environment -> idf.py -p %PORT% flash
set PORT=%1
if "%PORT%"=="" set PORT=COM14
cd /d %~dp0
rem Kill leftover idf_monitor.py (python) that may still hold the COM port
pwsh -NoProfile -Command "Get-CimInstance Win32_Process | Where-Object { $_.Name -eq 'python.exe' -and $_.CommandLine -match 'esp_idf_monitor|idf_monitor\.py' } | ForEach-Object { Write-Host ('Stop leftover monitor: PID ' + $_.ProcessId); Stop-Process -Id $_.ProcessId -Force; Start-Sleep -Milliseconds 500 }"
pwsh -NoProfile -ExecutionPolicy Bypass -Command ". 'C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1' *> $null; idf.py -p %PORT% flash; exit $LASTEXITCODE"
pause
