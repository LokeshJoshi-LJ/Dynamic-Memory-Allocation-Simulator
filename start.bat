@echo off
cd /d "%~dp0"

if not exist "backend.exe" (
    echo ERROR: backend.exe not found!
    pause
    exit /b 1
)

echo Starting Dynamic Memory Allocation Simulator...

start "DMA Simulator Server" "%~dp0backend.exe"

timeout /t 2 /nobreak >nul

start "" "http://localhost:8080"

exit /b 0

