@echo off
cd /d "%~dp0"
where node >nul 2>nul
if errorlevel 1 (
 echo Install Node.js 22 or newer first.
 pause
 exit /b 1
)
call npm ci
if errorlevel 1 goto fail
call npm run build
if errorlevel 1 goto fail
echo Open http://localhost:3000 on this PC.
echo Friends: open http://YOUR-LAN-IP:3000 on the same network.
echo Find your IPv4 address below. Allow Node.js in Windows Firewall for private networks.
ipconfig
call npm run server
:fail
pause
