<# 
EdgeWind 服务器控制台（改进版 - 彩色交互界面）

功能：
- 彩色菜单与实时状态显示
- 快捷键支持（S/X/R/T/Q）
- 自动检测 Python 3.11 + venv311
- 端口策略（默认固定 5000，避免硬件端口不一致；可通过环境变量允许回退）
- 后台启动，关闭终端不影响服务

用法：
- 交互菜单：双击 服务器开关.bat 或运行 edgewind_ctl.bat
- 非交互：edgewind_ctl.bat start|stop|restart|status|toggle
#>

param(
  [ValidateSet('', 'start', 'stop', 'restart', 'status', 'toggle')]
  [string]$Action = ''
)

$ErrorActionPreference = 'Stop'
# 说明：本脚本放在 scripts/ 下，项目根目录为其上一级
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $ProjectRoot

# 控制台输出编码（避免中文乱码）
try {
  cmd /c "chcp 65001 >nul" | Out-Null
  [Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
  $OutputEncoding = [Console]::OutputEncoding
} catch {
  # 忽略编码设置失败
}

# ---------------------------
# UI 帮助函数（对齐/宽度自适应）
# 说明：Windows 控制台里中文/部分符号可能按“2列宽”显示，直接用字符数居中会偏。
# 这里用一个轻量的“显示宽度”估算，确保框线/标题在不同终端里更稳定对齐。
# ---------------------------
function Get-CharDisplayWidth([char]$c) {
  $code = [int]$c
  # 常见全角/中日韩/符号范围（近似 wcwidth）
  if (
    ($code -ge 0x1100 -and $code -le 0x115F) -or  # Hangul Jamo init
    ($code -ge 0x2E80 -and $code -le 0xA4CF) -or  # CJK Radicals .. Yi
    ($code -ge 0xAC00 -and $code -le 0xD7A3) -or  # Hangul Syllables
    ($code -ge 0xF900 -and $code -le 0xFAFF) -or  # CJK Compatibility Ideographs
    ($code -ge 0xFE10 -and $code -le 0xFE6F) -or  # Vertical/compat forms
    ($code -ge 0xFF00 -and $code -le 0xFF60) -or  # Fullwidth forms
    ($code -ge 0xFFE0 -and $code -le 0xFFE6)      # Fullwidth symbol variants
  ) { return 2 }
  return 1
}

function Get-DisplayWidth([string]$s) {
  if ([string]::IsNullOrEmpty($s)) { return 0 }
  $w = 0
  foreach ($ch in $s.ToCharArray()) {
    $w += (Get-CharDisplayWidth $ch)
  }
  return $w
}

function Truncate-ToWidth([string]$s, [int]$maxWidth) {
  if ([string]::IsNullOrEmpty($s)) { return '' }
  if ($maxWidth -le 0) { return '' }
  $w = 0
  $sb = New-Object System.Text.StringBuilder
  foreach ($ch in $s.ToCharArray()) {
    $cw = Get-CharDisplayWidth $ch
    if (($w + $cw) -gt $maxWidth) { break }
    [void]$sb.Append($ch)
    $w += $cw
  }
  return $sb.ToString()
}

function Get-UiInnerWidth {
  # 目标：不依赖固定字符数，按当前窗口宽度自适应；同时给一个上限避免框太宽。
  $desired = 55
  $min = 20
  try {
    $w = [Console]::WindowWidth
    # 留出左右边框 2 列
    $inner = [Math]::Min($desired, [Math]::Max($min, $w - 2))
    return [int]$inner
  } catch {
    return $desired
  }
}

function Get-UiChars {
  # 如遇到某些终端/字体对框线字符兼容性差，可设置：
  #   set EDGEWIND_ASCII_UI=true
  $asciiRaw = $env:EDGEWIND_ASCII_UI
  if ($null -eq $asciiRaw) { $asciiRaw = '' }
  $useAscii = ($asciiRaw.ToString().Trim().ToLowerInvariant() -eq 'true')

  if ($useAscii) {
    return @{
      TL = '+'
      TR = '+'
      BL = '+'
      BR = '+'
      H  = '-'
      V  = '|'
      SEP = '-'
    }
  }

  return @{
    TL = '╔'
    TR = '╗'
    BL = '╚'
    BR = '╝'
    H  = '═'
    V  = '║'
    SEP = '─'
  }
}

function Write-Separator([int]$innerWidth, [string]$color = 'DarkGray') {
  $ui = Get-UiChars
  Write-Host (($ui.SEP * $innerWidth)) -ForegroundColor $color
}

function Write-SectionHeader([string]$title, [string]$lineColor, [string]$titleColor) {
  $inner = Get-UiInnerWidth
  $ui = Get-UiChars
  Write-Host (($ui.H * $inner)) -ForegroundColor $lineColor
  Write-Host ("  " + $title) -ForegroundColor $titleColor
  Write-Host (($ui.H * $inner)) -ForegroundColor $lineColor
}

function Get-ListeningPids([int]$Port) {
  $lines = netstat -ano | Select-String -Pattern "LISTENING"
  $pids = @()
  foreach ($line in $lines) {
    $s = $line.ToString()
    if ($s -match (":$Port\s+.*LISTENING\s+(\d+)\s*$")) {
      $pids += [int]$Matches[1]
    }
  }
  return ($pids | Sort-Object -Unique)
}

function Get-LanIPv4List {
  <#
    获取本机可用于“局域网访问”的 IPv4 地址列表。
    - 过滤：127.0.0.1、169.254.x.x（APIPA）、0.0.0.0
    - 优先：Get-NetIPAddress（Win10/11 通用）
    - 回退：解析 ipconfig 输出，避免某些环境缺少 NetTCPIP 模块
  #>
  $ips = @()
  try {
    $ips = Get-NetIPAddress -AddressFamily IPv4 -ErrorAction Stop |
      Where-Object {
        $_.IPAddress -and
        $_.IPAddress -ne '127.0.0.1' -and
        $_.IPAddress -ne '0.0.0.0' -and
        ($_.IPAddress -notlike '169.254.*')
      } |
      Select-Object -ExpandProperty IPAddress
  } catch {
    try {
      $out = cmd /c "ipconfig"
      foreach ($line in $out) {
        # 兼容中文/英文系统：IPv4 地址/IPv4 Address
        if ($line -match '(IPv4.*地址|IPv4 Address)[^:]*:\s*([\d\.]+)') {
          $ip = $Matches[2]
          if ($ip -and $ip -ne '127.0.0.1' -and $ip -ne '0.0.0.0' -and ($ip -notlike '169.254.*')) {
            $ips += $ip
          }
        }
      }
    } catch {
      # 忽略
    }
  }

  return ($ips | Sort-Object -Unique)
}

function Write-AccessUrls([int]$Port) {
  $lanIps = Get-LanIPv4List

  Write-Host "  访问地址: " -NoNewline -ForegroundColor White
  Write-Host "http://localhost:$Port" -ForegroundColor Cyan

  if ($lanIps.Count -gt 0) {
    Write-Host "  局域网访问: " -ForegroundColor White
    foreach ($ip in $lanIps) {
      Write-Host "    - http://$ip`:$Port" -ForegroundColor Cyan
    }
    Write-Host "  提示: 需同一局域网/同网段，且防火墙放行该端口" -ForegroundColor DarkGray
  } else {
    Write-Host "  局域网访问: 未检测到可用 IPv4 地址（可能未联网/无网卡）" -ForegroundColor DarkYellow
  }
}

function Stop-EdgeWind {
  Write-Host ""
  Write-SectionHeader -title "停止服务" -lineColor DarkRed -titleColor Red
  Write-Host ""

  $stopBat = Join-Path $ProjectRoot 'stop_edgewind.bat'
  if (Test-Path $stopBat) {
    cmd /c "`"$stopBat`"" | Out-Null
  } else {
    foreach ($p in @(5000, 5002)) {
      foreach ($procId in (Get-ListeningPids $p)) {
        if ($procId -eq 4) {
          Write-Host "  [!] 端口 $p 被 System(PID 4) 占用，无法结束" -ForegroundColor Yellow
          continue
        }
        try {
          $proc = Get-Process -Id $procId -ErrorAction Stop
          if ($proc.Path -and ($proc.Path -match 'python\.exe$')) {
            Write-Host "  [√] 结束 PID=$procId (python.exe) 端口=$p" -ForegroundColor Green
            Stop-Process -Id $procId -Force
          } else {
            Write-Host "  [!] PID=$procId 不是 python.exe，跳过" -ForegroundColor DarkYellow
          }
        } catch {
          # 进程已不存在
        }
      }
    }
  }

  Write-Host ""
  Write-Host "  [√] 停止完成" -ForegroundColor Green
  Write-Host ""
}

function Ensure-Venv311 {
  # 检查 Python 3.11
  $null = cmd /c "py -3.11 -V 2>nul"
  if ($LASTEXITCODE -ne 0) {
    Write-Host "  [×] 未检测到 Python 3.11" -ForegroundColor Red
    Write-Host "  请先安装 Python 3.11，并确保 'py -3.11 -V' 可用" -ForegroundColor Yellow
    throw "Python 3.11 未安装"
  }

  $venvPy = Join-Path $ProjectRoot 'venv311\Scripts\python.exe'
  if (-not (Test-Path $venvPy)) {
    Write-Host "  [i] 未检测到 venv311，开始创建..." -ForegroundColor Cyan
    cmd /c "py -3.11 -m venv venv311" | Out-Null
    cmd /c "`"$venvPy`" -m pip install --upgrade pip" | Out-Null
    cmd /c "`"$venvPy`" -m pip install -r requirements.txt" | Out-Null
    Write-Host "  [√] venv311 创建完成" -ForegroundColor Green
  }
  return $venvPy
}

function Pick-Port {
  # 默认：固定 5000（与硬件固件 SERVER_PORT=5000 保持一致，避免后端偷偷切到 5002 导致硬件 TIMEOUT）
  # 如需允许自动回退到 5002：
  #   $env:EDGEWIND_ALLOW_FALLBACK_PORT="true"
  # 注意：PowerShell 的逻辑运算符 `-or` 返回的是 [bool]，不能用来做“空字符串回退”。
  # 这里用兼容 Windows PowerShell 5.1 的方式安全读取环境变量。
  $fallbackRaw = $env:EDGEWIND_ALLOW_FALLBACK_PORT
  if ($null -eq $fallbackRaw) { $fallbackRaw = '' }
  $allowFallback = ($fallbackRaw.ToString().Trim().ToLowerInvariant() -eq 'true')

  if ((Get-ListeningPids 5000).Count -eq 0) { return 5000 }

  if ($allowFallback -and (Get-ListeningPids 5002).Count -eq 0) {
    Write-Host "  [!] 端口 5000 被占用，已回退到 5002（注意：硬件端需同步修改端口）" -ForegroundColor Yellow
    return 5002
  }

  Write-Host "  [×] 端口 5000 被占用，服务将无法启动。" -ForegroundColor Red
  Write-Host "      建议：释放 5000 端口，或设置 EDGEWIND_ALLOW_FALLBACK_PORT=true 允许回退到 5002（并修改硬件端口）" -ForegroundColor Yellow
  return 5000
}

function Start-EdgeWind {
  Write-Host ""
  Write-SectionHeader -title "启动服务" -lineColor DarkGreen -titleColor Green
  Write-Host ""

  try {
    $venvPy = Ensure-Venv311
  } catch {
    Write-Host ""
    return
  }

  $port = Pick-Port

  if (-not (Test-Path (Join-Path $ProjectRoot 'logs'))) {
    New-Item -ItemType Directory -Path (Join-Path $ProjectRoot 'logs') | Out-Null
  }

  $env:FORCE_ASYNC_MODE = 'eventlet'
  $env:PORT = "$port"
  $env:ALLOWED_ORIGINS = '*'

  $stdout = Join-Path $ProjectRoot 'logs\server_eventlet_stdout.log'
  $stderr = Join-Path $ProjectRoot 'logs\server_eventlet_stderr.log'

  Write-Host "  [i] Python: 3.11.6 (venv311)" -ForegroundColor Cyan
  Write-Host "  [i] 异步模式: eventlet" -ForegroundColor Cyan
  Write-Host "  [i] 端口: $port" -ForegroundColor Cyan
  Write-Host ""

  # 后台启动，关闭终端不影响服务
  Start-Process -FilePath $venvPy -ArgumentList '.\app.py' -WorkingDirectory $ProjectRoot `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr -WindowStyle Hidden | Out-Null

  Write-Host "  [i] 正在启动，请稍候..." -ForegroundColor Cyan

  # 循环检测启动状态（最多 10 次，每次 0.5 秒）
  $maxAttempts = 10
  $attempt = 0
  $started = $false

  while ($attempt -lt $maxAttempts) {
    Start-Sleep -Milliseconds 500
    $attempt++
    
    $pids = Get-ListeningPids $port
    if ($pids.Count -gt 0) {
      $started = $true
      break
    }
  }

  Write-Host ""
  
  # 验证启动结果
  if ($started) {
    Write-Host "  [√] 启动成功！" -ForegroundColor Green
    Write-Host ""
    Write-AccessUrls -Port $port
    Write-Host "  Admin: Edge_Wind (pwd: see first-start log, or set EDGEWIND_ADMIN_INIT_PASSWORD)" -ForegroundColor DarkGray
    Write-Host "  日志文件: logs\server_eventlet_stdout.log" -ForegroundColor DarkGray
  } else {
    Write-Host "  [!] 端口未在 5 秒内监听，可能启动较慢" -ForegroundColor Yellow
    Write-Host "  请稍候片刻后访问: http://localhost:$port" -ForegroundColor Cyan
    $lanIps = Get-LanIPv4List
    if ($lanIps.Count -gt 0) {
      Write-Host "  或通过局域网访问:" -ForegroundColor White
      foreach ($ip in $lanIps) {
        Write-Host "    - http://$ip`:$port" -ForegroundColor Cyan
      }
    }
    Write-Host "  或检查日志: logs\server_eventlet_stderr.log" -ForegroundColor DarkGray
  }
  Write-Host ""
}

function Show-Status {
  Write-Host ""
  Write-SectionHeader -title "服务器状态" -lineColor DarkCyan -titleColor Cyan
  Write-Host ""

  $lanIps = Get-LanIPv4List
  $hasListener = $false
  foreach ($p in @(5000, 5002)) {
    $pids = Get-ListeningPids $p
    if ($pids.Count -eq 0) {
      Write-Host "  端口 $p : " -NoNewline -ForegroundColor White
      Write-Host "未监听" -ForegroundColor DarkGray
      continue
    }
    $hasListener = $true

    # 显示访问地址（包含局域网）
    Write-Host "  访问地址: " -NoNewline -ForegroundColor White
    Write-Host "http://localhost:$p" -ForegroundColor Cyan
    if ($lanIps.Count -gt 0) {
      Write-Host "  局域网访问: " -ForegroundColor White
      foreach ($ip in $lanIps) {
        Write-Host "    - http://$ip`:$p" -ForegroundColor Cyan
      }
    }
    Write-Host ""

    foreach ($procId in $pids) {
      try {
        $proc = Get-Process -Id $procId -ErrorAction Stop
        $path = $proc.Path
        Write-Host "  端口 $p : " -NoNewline -ForegroundColor White
        Write-Host "运行中" -NoNewline -ForegroundColor Green
        Write-Host " (PID=$procId)" -ForegroundColor DarkGray
        Write-Host "    进程: $($proc.ProcessName)" -ForegroundColor DarkGray
        if ($path -match 'Python311') {
          Write-Host "    版本: Python 3.11 + eventlet" -ForegroundColor DarkGreen
        } else {
          Write-Host "    路径: $path" -ForegroundColor DarkGray
        }
      } catch {
        Write-Host "  端口 $p : " -NoNewline -ForegroundColor White
        Write-Host "PID=$procId (进程信息不可获取)" -ForegroundColor DarkYellow
      }
    }
  }

  if (-not $hasListener) {
    Write-Host "  [i] 服务器未运行" -ForegroundColor Yellow
  }

  Write-Host ""
  Write-Host "  日志位置: logs\server_eventlet_stdout.log" -ForegroundColor DarkGray
  Write-Host ""
}

function Is-Running {
  return ((Get-ListeningPids 5000).Count -gt 0) -or ((Get-ListeningPids 5002).Count -gt 0)
}

function Get-RunningPorts {
  $ports = @()
  foreach ($p in @(5000, 5002)) {
    if ((Get-ListeningPids $p).Count -gt 0) { $ports += $p }
  }
  return ($ports | Sort-Object -Unique)
}

function Toggle-EdgeWind {
  if (Is-Running) { Stop-EdgeWind } else { Start-EdgeWind }
}

function Show-Banner {
  Clear-Host
  Write-Host ""
  $inner = Get-UiInnerWidth
  $ui = Get-UiChars

  $title = 'EdgeWind 服务器控制台'
  $title = Truncate-ToWidth $title $inner
  $titleW = Get-DisplayWidth $title
  if ($titleW -gt $inner) { $titleW = $inner }

  $leftPad = [int][Math]::Floor(($inner - $titleW) / 2)
  $rightPad = [int]($inner - $titleW - $leftPad)

  Write-Host ($ui.TL + ($ui.H * $inner) + $ui.TR) -ForegroundColor Cyan
  Write-Host ($ui.V + (' ' * $inner) + $ui.V) -ForegroundColor Cyan

  # 标题行（分段上色）：边框/填充 Cyan，标题 White
  Write-Host $ui.V -NoNewline -ForegroundColor Cyan
  Write-Host (' ' * $leftPad) -NoNewline -ForegroundColor Cyan
  Write-Host $title -NoNewline -ForegroundColor White
  Write-Host (' ' * $rightPad) -NoNewline -ForegroundColor Cyan
  Write-Host $ui.V -ForegroundColor Cyan

  Write-Host ($ui.V + (' ' * $inner) + $ui.V) -ForegroundColor Cyan
  Write-Host ($ui.BL + ($ui.H * $inner) + $ui.BR) -ForegroundColor Cyan
  Write-Host ""
}

function Menu {
  while ($true) {
    Show-Banner
    
    # 显示当前状态
    $running = Is-Running
    if ($running) {
      Write-Host "  当前状态: " -NoNewline
      Write-Host "● 运行中" -ForegroundColor Green

      $ports = Get-RunningPorts
      $lanIps = Get-LanIPv4List
      foreach ($p in $ports) {
        Write-Host "  访问地址: " -NoNewline -ForegroundColor White
        Write-Host "http://localhost:$p" -ForegroundColor Cyan
        if ($lanIps.Count -gt 0) {
          Write-Host "  局域网访问: " -ForegroundColor White
          foreach ($ip in $lanIps) {
            Write-Host "    - http://$ip`:$p" -ForegroundColor Cyan
          }
        }
      }
    } else {
      Write-Host "  当前状态: " -NoNewline
      Write-Host "○ 已停止" -ForegroundColor DarkGray
    }
    Write-Host ""
    $inner = Get-UiInnerWidth
    Write-Separator -innerWidth $inner -color DarkGray
    Write-Host ""
    Write-Host "  [S] 启动服务 (Python 3.11 + eventlet)" -ForegroundColor Green
    Write-Host "  [X] 停止服务 (5000/5002)" -ForegroundColor Red
    Write-Host "  [R] 重启服务" -ForegroundColor Yellow
    Write-Host "  [T] 一键切换 (运行→停止 / 停止→启动)" -ForegroundColor Cyan
    Write-Host "  [I] 查看状态" -ForegroundColor Blue
    Write-Host "  [Q] 退出" -ForegroundColor DarkGray
    Write-Host ""
    Write-Separator -innerWidth $inner -color DarkGray
    Write-Host ""
    
    $sel = Read-Host "  请选择"
    
    switch ($sel.ToUpperInvariant()) {
      'S' { Start-EdgeWind; Read-Host "  按回车继续" | Out-Null }
      'X' { Stop-EdgeWind; Read-Host "  按回车继续" | Out-Null }
      'R' { Stop-EdgeWind; Start-EdgeWind; Read-Host "  按回车继续" | Out-Null }
      'T' { Toggle-EdgeWind; Read-Host "  按回车继续" | Out-Null }
      'I' { Show-Status; Read-Host "  按回车继续" | Out-Null }
      'Q' { 
        Write-Host ""
        Write-Host "  再见！" -ForegroundColor Cyan
        Write-Host ""
        return 
      }
      default { 
        Write-Host ""
        Write-Host "  [!] 无效输入，请重试" -ForegroundColor Yellow
        Start-Sleep -Seconds 1
      }
    }
  }
}

# 主入口
switch ($Action) {
  'start'   { Start-EdgeWind }
  'stop'    { Stop-EdgeWind }
  'restart' { Stop-EdgeWind; Start-EdgeWind }
  'status'  { Show-Status }
  'toggle'  { Toggle-EdgeWind }
  default   { Menu }
}


