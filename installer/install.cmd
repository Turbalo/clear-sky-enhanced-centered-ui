@echo off
setlocal
powershell.exe -NoProfile -STA -ExecutionPolicy Bypass -File "%~dp0install.ps1" -Action Install -Interactive
if errorlevel 1 pause
