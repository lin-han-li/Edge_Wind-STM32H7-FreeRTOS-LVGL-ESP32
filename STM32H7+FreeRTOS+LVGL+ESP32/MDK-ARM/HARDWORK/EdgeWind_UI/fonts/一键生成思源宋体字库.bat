@echo off
setlocal EnableExtensions

chcp 65001 >nul

REM Run in this folder
cd /d "%~dp0"

echo.
echo [EdgeWind] Generating SourceHanSerif fonts (LVGL)...
echo Folder: %cd%
echo.

REM Use Bypass to avoid execution policy blocking on double-click
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0gen_sourcehanserif_fonts.ps1"

echo.
echo Done. If no errors above, rebuild Keil project.
echo.
pause
endlocal
