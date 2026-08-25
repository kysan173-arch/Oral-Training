@echo off
rem ============================================================
rem  Oral Training Backend - one-click launcher
rem  Double-click this file to start the backend server.
rem  This window stays open while the backend is running.
rem  Press Ctrl+C to stop the server.
rem  Requires: PostgreSQL installed & running, backend.env configured.
rem ============================================================
setlocal
cd /d "%~dp0"

title Oral Training Backend

echo ============================================================
echo   Oral Training Backend - startup
echo   Address: see backend.env (BIND_ADDRESS : PORT)
echo ============================================================
echo.

rem ---- 1. backend.env exists? ----
if not exist "%~dp0backend.env" (
  if exist "%~dp0backend.env.example" (
    copy /y "%~dp0backend.env.example" "%~dp0backend.env" >nul
    echo [INFO] Created backend.env. Edit the database password and DeepSeek API Key, then run again.
    echo.
    pause
    exit /b 1
  ) else (
    echo [ERROR] Missing backend.env and backend.env.example.
    echo.
    pause
    exit /b 1
  )
)

rem ---- 2. executable exists? ----
if not exist "%~dp0oral_training_backend.exe" (
  echo [ERROR] oral_training_backend.exe not found.
  echo         Build first, then run start-backend.cmd again.
  echo.
  pause
  exit /b 1
)

rem ---- 3. launch via start-backend.ps1 (loads env, checks PG, starts service) ----
echo [OK] Starting backend...
echo      Keep this window open. Press Ctrl+C to stop the server.
echo.
echo      The API will be available at:
echo        http://127.0.0.1:8080/api     (adjust if PORT differs in backend.env)
echo.

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0start-backend.ps1"
if errorlevel 1 (
  echo.
  echo [ERROR] Backend failed to start. See output above.
  echo.
  pause
  exit /b 1
)

echo.
echo [INFO] Backend has exited.
pause
endlocal
