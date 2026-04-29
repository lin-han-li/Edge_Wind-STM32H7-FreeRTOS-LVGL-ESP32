@echo off
setlocal

REM 添加防火墙规则以允许局域网访问EdgeWind系统
REM 需要管理员权限运行

echo ====================================
echo  EdgeWind - 添加防火墙规则
echo ====================================
echo.

REM 检查管理员权限
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo 错误: 需要管理员权限！
    echo.
    echo 请右键此文件，选择"以管理员身份运行"
    echo.
    pause
    exit /b 1
)

echo [1/2] 正在添加5000端口的防火墙规则...
netsh advfirewall firewall add rule name="EdgeWind-5000-TCP" dir=in action=allow protocol=TCP localport=5000 >nul 2>&1

if %errorLevel% equ 0 (
    echo [成功] 防火墙规则已添加
) else (
    echo [提示] 规则可能已存在，尝试删除旧规则...
    netsh advfirewall firewall delete rule name="EdgeWind-5000-TCP" >nul 2>&1
    netsh advfirewall firewall add rule name="EdgeWind-5000-TCP" dir=in action=allow protocol=TCP localport=5000 >nul 2>&1
    echo [成功] 防火墙规则已更新
)

echo.
echo [2/2] 获取本机IP地址...
for /f "tokens=2 delims=:" %%a in ('ipconfig ^| findstr /C:"IPv4"') do (
    echo     局域网访问地址: http://%%a:5000
)

echo.
echo ====================================
echo  配置完成！
echo ====================================
echo.
echo 现在可以通过局域网访问EdgeWind系统了
echo.
echo 提示: 如果还是无法访问，请检查：
echo   1. 服务器是否正在运行（双击“服务器开关.bat”启动）
echo   2. 设备是否在同一局域网内
echo   3. 杀毒软件是否阻止了连接
echo.

pause

endlocal
