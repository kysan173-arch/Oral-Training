@echo off
cd /d "%~dp0"

echo ==========================================
echo  口腔客服智能陪练 - DeepSeek 后端启动
echo ==========================================
echo.

:: 检查并杀掉占用 8080 端口的所有进程
for /f "tokens=5" %%a in ('netstat -ano ^| findstr :8080.*LISTENING') do (
    echo [信息] 发现进程 %%a 占用 8080 端口，正在终止...
    taskkill /PID %%a /F >nul 2>&1
)

:: 等待端口释放
timeout /t 1 /nobreak >nul

:: 启动 C++ DeepSeek 后端
echo [信息] 正在启动 DeepSeek 后端...
cd newbackend
call start-backend.cmd

echo.
echo [完成] DeepSeek 后端已启动
echo        地址: http://127.0.0.1:8080/api
echo        健康检查: http://127.0.0.1:8080/api/health
echo.
pause
