@echo off
rem Starts the AeroDSL demonstration page and opens it in your browser.
rem Close the "AeroDSL server" window to stop it.
cd /d "%~dp0"
start "AeroDSL server" bin\windows-x64\aerodsl.exe --serve 8080
timeout /t 1 /nobreak >nul
start "" http://127.0.0.1:8080
