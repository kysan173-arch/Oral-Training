@echo off
setlocal EnableExtensions

rem ============================================================
rem  Oral Training Backend - one-click launcher
rem
rem  Double-click this file to start the server.
rem  The window stays open while the server runs; Ctrl+C stops it.
rem
rem  NEW: every run also writes a transcript to start-backend.log,
rem  next to this file. If the window ever flashes and vanishes
rem  again, open that log - its last line says exactly how far the
rem  script got. A window can no longer hide the reason.
rem
rem  Steps, in this exact order - the order matters:
rem    1. check that backend.env exists
rem    2. load backend.env into this process environment
rem       the server reads config ONLY from env vars, never from the file
rem    3. make sure the PostgreSQL service is up
rem    4. stop any backend still holding the port
rem       must run BEFORE the exe is replaced: Windows locks a running
rem       executable, so refreshing it earlier would fail and the rebuild
rem       would silently never reach the server
rem    5. re-deploy the newest build output over the deployed exe
rem    6. run the server
rem
rem  Requires: PostgreSQL installed, backend.env configured, and a build
rem            at backend\build-msvc\Release\oral_training_backend.exe
rem ============================================================

rem ---- flight recorder: written before anything else can fail ----
set "LOG=%~dp0start-backend.log"
>"%LOG%" echo [%DATE% %TIME%] launcher invoked
>>"%LOG%" echo   script  = %~f0
>>"%LOG%" echo   cwd     = %CD%
>>"%LOG%" echo   cmdline = %CMDCMDLINE%
>>"%LOG%" echo   args    = [%*]

cd /d "%~dp0"
title Oral Training Backend

echo ============================================================
echo   Oral Training Backend - startup
echo ============================================================
echo.

rem ---- 1/6: config file present? ----
echo [1/6] Checking backend.env ...
>>"%LOG%" echo [1/6] checking backend.env
if not exist "%~dp0backend.env" (
  if exist "%~dp0backend.env.example" (
    copy /y "%~dp0backend.env.example" "%~dp0backend.env" >nul
    echo [INFO] Created backend.env from the example.
    echo        Edit the database password and the DeepSeek API key, then run this file again.
    >>"%LOG%" echo [INFO] created backend.env from the example
  ) else (
    echo [ERROR] Missing backend.env, and no backend.env.example to copy from.
    >>"%LOG%" echo [ERROR] missing backend.env, no example to copy from
  )
  echo.
  pause
  exit /b 1
)

rem ---- 2/6: load backend.env ----
rem Skip comment lines. Split on the FIRST '=' only, so values may contain '='.
rem Keys are written straight into the environment; the server reads them from there.
echo [2/6] Loading backend.env ...
for /f "usebackq tokens=1,* delims==" %%A in (`findstr /v /b /c:"#" "%~dp0backend.env"`) do (
  if not "%%~A"=="" set "%%A=%%B"
)
if not defined PORT set "PORT=8080"
if not defined BIND_ADDRESS set "BIND_ADDRESS=127.0.0.1"
rem Report what was loaded, without printing any secret value.
if defined DATABASE_URL (
  echo   DATABASE_URL: loaded
  >>"%LOG%" echo [2/6] backend.env loaded
) else (
  echo   [WARN] DATABASE_URL is empty - check the first line of backend.env.
  >>"%LOG%" echo [2/6] [WARN] DATABASE_URL is empty
)
echo   BIND_ADDRESS=%BIND_ADDRESS%   PORT=%PORT%
>>"%LOG%" echo   BIND_ADDRESS=%BIND_ADDRESS%  PORT=%PORT%

rem ---- 3/6: PostgreSQL service ----
echo [3/6] Checking PostgreSQL service ...
>>"%LOG%" echo [3/6] checking postgresql service
net start | findstr /i /c:"postgresql" >nul
if errorlevel 1 (
  echo   Not running - attempting to start "postgresql-x64-18" ...
  net start "postgresql-x64-18" >nul 2>&1
  if errorlevel 1 (
    echo   [WARN] Could not start PostgreSQL - name differs, or this window is not elevated.
    echo          Start PostgreSQL yourself if the server cannot reach the database.
    >>"%LOG%" echo [3/6] [WARN] could not start postgresql
  ) else (
    echo   PostgreSQL started.
    >>"%LOG%" echo [3/6] postgresql started
  )
) else (
  echo   PostgreSQL is already running.
  >>"%LOG%" echo [3/6] postgresql already running
)

rem ---- 4/6: stop stale instances, before touching the exe ----
echo [4/6] Stopping any backend still running ...
>>"%LOG%" echo [4/6] stopping stale backends on port %PORT%
rem Only processes whose image really is our backend are killed; if the port
rem belongs to some other program it is left alone and reported instead.
for /f "tokens=5" %%P in ('netstat -ano ^| findstr ":%PORT%" ^| findstr /i "LISTENING"') do (
  tasklist /fi "PID eq %%P" /fi "IMAGENAME eq oral_training_backend.exe" 2>nul | findstr /i "oral_training_backend.exe" >nul
  if not errorlevel 1 (
    echo   Stopping stale backend, PID %%P ...
    >>"%LOG%" echo   killing stale backend PID %%P
    taskkill /f /pid %%P >nul 2>&1
  ) else (
    echo   [WARN] Port %PORT% is held by another program, PID %%P - not touching it.
    >>"%LOG%" echo   [WARN] port held by another program, PID %%P
  )
)
rem Catch instances listening on other ports too: any of them keeps the exe locked.
taskkill /f /im oral_training_backend.exe >nul 2>&1
rem Give Windows a moment to release the file lock.
ping -n 2 127.0.0.1 >nul 2>&1

rem ---- 5/6: deploy the newest build ----
echo [5/6] Deploying the newest build ...
set "BUILD_EXE=%~dp0build-msvc\Release\oral_training_backend.exe"
set "DEPLOY_EXE=%~dp0oral_training_backend.exe"
if exist "%BUILD_EXE%" (
  rem Always re-copy instead of comparing timestamps: copying only when the
  rem deployed file is missing keeps launching a stale binary after every
  rem rebuild, which looks exactly like the new route not existing.
  copy /y "%BUILD_EXE%" "%DEPLOY_EXE%" >nul
  if errorlevel 1 (
    echo   [ERROR] Cannot replace oral_training_backend.exe - the file is still locked.
    echo          Close every running backend window, then try again.
    >>"%LOG%" echo [5/6] [ERROR] cannot replace exe, still locked
    echo.
    pause
    exit /b 1
  )
  echo   Copied build-msvc\Release\oral_training_backend.exe
  >>"%LOG%" echo [5/6] copied build output over deployed exe
) else (
  if not exist "%DEPLOY_EXE%" (
    echo   [ERROR] No executable found at all. Build the backend first:
    echo           cmake --build build-msvc --config Release
    >>"%LOG%" echo [5/6] [ERROR] no executable found
    echo.
    pause
    exit /b 1
  )
  echo   [WARN] No build output found - using the deployed executable as-is.
  >>"%LOG%" echo [5/6] [WARN] no build output, using deployed exe
)
rem Print the deployed binary timestamp: it is the quickest way to confirm
rem that the process about to start really is the build you just compiled.
for %%I in ("%DEPLOY_EXE%") do echo   Executable timestamp: %%~tI
for %%I in ("%DEPLOY_EXE%") do >>"%LOG%" echo   exe timestamp: %%~tI
if not exist "%~dp0libpq.dll" (
  echo   [ERROR] libpq.dll is missing next to the executable.
  >>"%LOG%" echo [5/6] [ERROR] libpq.dll missing
  echo.
  pause
  exit /b 1
)

rem ---- 6/6: run ----
echo [6/6] Starting backend server ...
echo.
echo   API base: http://%BIND_ADDRESS%:%PORT%/api
echo   Keep this window open. Press Ctrl+C to stop the server.
echo.
>>"%LOG%" echo [6/6] starting server on %BIND_ADDRESS%:%PORT%
set "PATH=%~dp0;%PATH%"
"%~dp0oral_training_backend.exe"
set "EXIT_CODE=%ERRORLEVEL%"
>>"%LOG%" echo server exited with code %EXIT_CODE%
echo.
echo [INFO] Backend has exited - code %EXIT_CODE%.
echo.
pause
endlocal
