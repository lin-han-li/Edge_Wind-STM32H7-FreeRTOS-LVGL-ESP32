@echo off
chcp 65001 >nul
setlocal

echo.
echo ═══════════════════════════════════════════════
echo   EdgeWind - Git 提交助手
echo ═══════════════════════════════════════════════
echo.

REM 检查是否在 Git 仓库中
git rev-parse --git-dir >nul 2>&1
if errorlevel 1 (
    echo [×] 错误：当前目录不是 Git 仓库
    pause
    exit /b 1
)

echo [步骤 1/4] 检查 Git 状态...
echo.
git status --short
echo.

echo [步骤 2/4] 检查是否有未暂存的更改...
set HAS_UNSTAGED=0
git diff --quiet
if errorlevel 1 set HAS_UNSTAGED=1

git diff --cached --quiet
if errorlevel 1 (
    echo [√] 发现已暂存的更改
) else (
    echo [!] 警告：没有已暂存的更改
    echo.
    echo 请先暂存要提交的文件：
    echo   1. 在 VS Code 中点击文件旁的 "+" 号暂存单个文件
    echo   2. 或点击"更改"旁的 "+" 号暂存所有更改
    echo   3. 或运行: git add .
    echo.
    pause
    exit /b 1
)

echo.
echo [步骤 3/4] 请输入提交信息（直接回车使用默认）:
set /p COMMIT_MSG="提交信息: "
if "%COMMIT_MSG%"=="" set COMMIT_MSG=更新项目文件

echo.
echo [步骤 4/4] 提交更改...
git commit -m "%COMMIT_MSG%"
if errorlevel 1 (
    echo.
    echo [×] 提交失败，请检查错误信息
    pause
    exit /b 1
)

echo.
echo [√] 提交成功！
echo.
echo [下一步] 推送到 GitHub...
echo 提示：如果推送很慢，可能是网络问题，请稍候...
echo.

git push origin main
if errorlevel 1 (
    echo.
    echo [!] 推送失败，尝试推送到 master 分支...
    git push origin master
    if errorlevel 1 (
        echo.
        echo [×] 推送失败，请检查：
        echo   1. 网络连接是否正常
        echo   2. GitHub 远程仓库地址是否正确
        echo   3. 是否有推送权限
        echo.
        echo 可以手动运行: git push origin main
        pause
        exit /b 1
    )
)

echo.
echo [√] 推送成功！
echo.
echo ═══════════════════════════════════════════════
echo   完成！所有更改已同步到 GitHub
echo ═══════════════════════════════════════════════
echo.
pause

endlocal
