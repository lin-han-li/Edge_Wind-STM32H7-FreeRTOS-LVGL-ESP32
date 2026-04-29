@echo off
setlocal enabledelayedexpansion
chcp 65001 >nul

REM 切到项目根目录（bat 在 tools 目录下）
pushd "%~dp0.."

echo ============================================================
echo 【一键生成 SD 卡资源 Payload】
echo ============================================================
echo.
echo [INFO] 当前目录: %CD%
echo [INFO] 脚本功能:
echo        1. 扫描工程 UI 文案中的中文字符
echo        2. 更新字符表: MDK-ARM\HARDWORK\EdgeWind_UI\fonts\chars_cn.txt
echo        3. 生成字体 bin: 12/14/16/20/30px 五个字号
echo        4. 生成拼音字典 bin
echo        5. 打包图标 bin
echo.

REM 优先使用 Windows 自带 py 启动器，其次用 python
set "PY_CMD="
where py >nul 2>&1 && set "PY_CMD=py -3"
if "%PY_CMD%"=="" (
  where python >nul 2>&1 && set "PY_CMD=python"
)

if "%PY_CMD%"=="" (
  echo [ERROR] 未检测到 Python 3！
  echo         请安装 Python 3.7+ 并确保在 PATH 中。
  echo         下载地址: https://www.python.org/downloads/
  popd
  pause
  exit /b 1
)

echo [INFO] Python 命令: %PY_CMD%
echo.

REM ============================================================
REM 保护自定义拼音词库：避免 make_sd_payload.py 覆盖 tools\pinyin\pinyin.txt
REM 做法：只读取 tools\pinyin\pinyin.txt 生成 pinyin_dict.bin，不往 sd_payload 里复制 pinyin.txt
REM ============================================================
set "PINYIN_SRC=tools\pinyin\pinyin.txt"

if exist "%PINYIN_SRC%" (
  echo [INFO] 使用自定义拼音词库: %PINYIN_SRC%
  %PY_CMD% "tools\make_sd_payload.py" --force-update-flag --pinyin-txt "%PINYIN_SRC%"
) else (
  echo [WARN] 未找到 %PINYIN_SRC% ，将按默认流程生成拼音字典（可能会跳过）。
  %PY_CMD% "tools\make_sd_payload.py" --force-update-flag
)
if errorlevel 1 (
  echo.
  echo ============================================================
  echo [ERROR] 生成失败，请查看上方错误信息。
  echo ============================================================
  popd
  pause
  exit /b 1
)

echo.
echo ============================================================
echo [SUCCESS] SD 卡资源已生成完毕！
echo ============================================================
echo.
echo 【下一步】拷贝到 SD 卡根目录：
echo   - tools\sd_payload\gui   -^> SD:\gui
echo   - tools\sd_payload\fonts -^> SD:\fonts
echo   - tools\sd_payload\pinyin -^> SD:\pinyin
echo.
echo ============================================================
echo.

popd
pause
exit /b 0

