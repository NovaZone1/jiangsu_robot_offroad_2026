@echo off
title Nova BLE Bridge - Restart
echo Close Nova COM12 before continuing.
echo This restarts the PC bridge only. It does NOT stop the motor.
pause
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Bridge.ps1" -Action Stop
if errorlevel 1 (
    pause
    exit /b 1
)
echo Waiting 25 seconds for the old BLE connection to be released...
timeout /t 25 /nobreak >nul
echo Starting bridge. Wait for READY, then open COM12 in Nova.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Bridge.ps1" -Action Bridge
pause
