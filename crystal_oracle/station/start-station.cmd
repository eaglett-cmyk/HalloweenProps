@echo off
rem Crystal Oracle station server (:8460). Opens the app in the browser if it is already running.
title Crystal Oracle station (:8460)
cd /d "%~dp0"
netstat -ano | findstr /r /c:":8460 .*LISTENING" >nul
if not errorlevel 1 (
  echo Station server is already running on :8460.
  start "" http://127.0.0.1:8460/
  timeout /t 3 >nul
  exit /b 0
)
echo Starting station server - iPad: http://192.168.1.56:8460/  (close this window to stop it)
node server.js
pause
