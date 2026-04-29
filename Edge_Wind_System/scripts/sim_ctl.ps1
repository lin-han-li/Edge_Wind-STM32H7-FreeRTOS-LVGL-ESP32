<# 
EdgeWind 模拟器控制台（彩色交互界面）

功能：
- 启动/停止/重启/状态/切换 sim.py
- 默认以【新窗口可交互】方式运行 sim.py（可在新窗口里输入 add/fault/clear 等命令）
- 显示：目标服务器地址 + 本机局域网 IPv4（便于确认局域网环境）

用法：
- 交互菜单：双击 模拟器开关.bat 或运行 sim_ctl.bat
- 非交互：sim_ctl.bat start|stop|restart|status|toggle
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

function Ensure-Venv311 {
  # 检查 Python 3.11（尽量与服务器保持一致）
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

function Get-LanIPv4List {
  <#
    获取本机可用于“局域网访问/联调”的 IPv4 地址列表。
    - 过滤：127.0.0.1、169.254.x.x（APIPA）、0.0.0.0
    - 优先：Get-NetIPAddress
    - 回退：解析 ipconfig 输出
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
        # 兼容中文/英文系统：IPv4 地址 / IPv4 Address
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

function Get-SimPids {
  <#
    通过 Win32_Process 的 CommandLine 精确识别 sim.py 进程。
    说明：sim.py 是客户端，不监听端口，不能像服务器那样通过 netstat 检测。
  #>
  $pids = @()
  try {
    $procs = Get-CimInstance Win32_Process -Filter "Name='python.exe'" -ErrorAction Stop
    foreach ($p in $procs) {
      $cmd = $p.CommandLine
      if ($cmd -and ($cmd -match '(?i)(^|\\|/|\s|")sim\.py(\s|"|$)')) {
        $pids += [int]$p.ProcessId
      }
    }
  } catch {
    # 某些环境可能禁用 CIM；回退到 Get-Process（只能粗略）
    foreach ($p in (Get-Process -Name python -ErrorAction SilentlyContinue)) {
      $pids += [int]$p.Id
    }
  }
  return ($pids | Sort-Object -Unique)
}

function Get-SimCommandLine([int]$ProcessId) {
  try {
    $p = Get-CimInstance Win32_Process -Filter "ProcessId=$ProcessId" -ErrorAction Stop
    return $p.CommandLine
  } catch {
    return $null
  }
}

function Pick-ServerTarget {
  <#
    交互选择 sim.py 的目标服务器地址：
    - 自动（不传参）：让 sim.py 自己按 127.0.0.1:5000/5002 兜底
    - 指定：--host/--port 或 --server
  #>
  # 非交互模式（带 action 参数）下：不要 Read-Host 阻塞，按环境变量/默认策略直接返回
  if ($Action -ne '') {
    $envServer = $env:EDGEWIND_SIM_SERVER_URL
    if ($envServer) {
      $u = ($envServer.Trim().TrimEnd('/'))
      return @{ args=@('--server',"$u"); display="$u" }
    }

    $envHost = $env:EDGEWIND_SIM_HOST
    $envPort = $env:EDGEWIND_SIM_PORT
    if ($envHost -or $envPort) {
      $h = '127.0.0.1'
      if ($envHost) { $h = $envHost }
      $h = $h.Trim()

      $p = '5000'
      if ($envPort) { $p = "$envPort" }
      $p = $p.Trim()
      return @{ args=@('--host',"$h",'--port',"$p"); display="http://$h`:$p" }
    }

    return @{ args=@(); display='自动(本机5000/5002兜底)' }
  }

  Write-Host ""
  Write-Host "  请选择目标服务器：" -ForegroundColor White
  Write-Host "   [1] 自动（推荐）：由 sim.py 自动尝试本机 5000/5002" -ForegroundColor Green
  Write-Host "   [2] 指定本机端口：127.0.0.1:5000 或 5002" -ForegroundColor Cyan
  Write-Host "   [3] 指定局域网服务器：输入 IP 与端口" -ForegroundColor Cyan
  Write-Host "   [4] 自定义完整 URL：例如 http://192.168.1.10:5002" -ForegroundColor Cyan

  $lanIps = Get-LanIPv4List
  if ($lanIps.Count -gt 0) {
    Write-Host ""
    Write-Host "  本机局域网 IPv4（供参考）：" -ForegroundColor DarkGray
    foreach ($ip in $lanIps) { Write-Host "   - $ip" -ForegroundColor DarkGray }
  }

  $sel = Read-Host "  选择(1-4，默认 1)"
  if (-not $sel) { $sel = '1' }

  switch ($sel) {
    '1' { return @{ args=@(); display='自动(本机5000/5002兜底)' } }
    '2' {
      $port = Read-Host "  请输入端口(5000/5002，默认5000)"
      if (-not $port) { $port = '5000' }
      return @{ args=@('--host','127.0.0.1','--port',"$port"); display="http://127.0.0.1:$port" }
    }
    '3' {
      $serverHost = Read-Host "  请输入服务器IP(例如 192.168.1.10)"
      $port = Read-Host "  请输入端口(默认5000)"
      if (-not $port) { $port = '5000' }
      return @{ args=@('--host',"$serverHost",'--port',"$port"); display="http://$serverHost`:$port" }
    }
    '4' {
      $url = Read-Host "  请输入完整URL(例如 http://192.168.1.10:5002)"
      $url = ($url.Trim().TrimEnd('/'))
      return @{ args=@('--server',"$url"); display="$url" }
    }
    default { return @{ args=@(); display='自动(本机5000/5002兜底)' } }
  }
}

function Start-Sim {
  Write-Host ""
  Write-SectionHeader -title "启动模拟器" -lineColor DarkGreen -titleColor Green
  Write-Host ""

  if ((Get-SimPids).Count -gt 0) {
    Write-Host "  [!] 检测到 sim.py 已在运行，建议先停止或重启" -ForegroundColor Yellow
    Write-Host ""
    return
  }

  try {
    $venvPy = Ensure-Venv311
  } catch {
    Write-Host ""
    return
  }

  $target = Pick-ServerTarget
  $simArgs = @('.\sim.py') + $target.args

  Write-Host ""
  Write-Host "  [i] Python: 3.11 + venv311" -ForegroundColor Cyan
  Write-Host "  [i] 目标服务器: $($target.display)" -ForegroundColor Cyan
  Write-Host "  [i] 运行方式: 新窗口（可交互输入控制台命令）" -ForegroundColor Cyan
  Write-Host ""

  # 新窗口运行（可交互）。/k 保持窗口不关闭，方便查看输出与输入命令。
  $cmd = "cd /d `"$ProjectRoot`" && `"$venvPy`" " + ($simArgs -join ' ')
  Start-Process -FilePath "cmd.exe" -ArgumentList "/k", $cmd -WindowStyle Normal | Out-Null

  Start-Sleep -Milliseconds 800
  $pids = Get-SimPids
  Write-Host ""
  if ($pids.Count -gt 0) {
    Write-Host "  [√] 模拟器已启动 (PID: $($pids -join ', '))" -ForegroundColor Green
    Write-Host "  提示: 在新窗口里输入：add 1 风机#1直流母线  开始注册节点" -ForegroundColor DarkGray
  } else {
    Write-Host "  [!] 未检测到 sim.py 进程，可能启动失败或被安全策略拦截" -ForegroundColor Yellow
    Write-Host "  请检查：是否弹出新窗口、是否有报错信息、或杀软拦截" -ForegroundColor DarkGray
  }
  Write-Host ""
}

function Stop-Sim {
  Write-Host ""
  Write-SectionHeader -title "停止模拟器" -lineColor DarkRed -titleColor Red
  Write-Host ""

  $pids = Get-SimPids
  if ($pids.Count -eq 0) {
    Write-Host "  [i] 未检测到 sim.py 正在运行" -ForegroundColor Yellow
    Write-Host ""
    return
  }

  Write-Host "  将结束以下 sim.py 进程：" -ForegroundColor White
  foreach ($procId in $pids) {
    $cmd = Get-SimCommandLine $procId
    if ($cmd) {
      Write-Host "   - PID=$procId  $cmd" -ForegroundColor DarkGray
    } else {
      Write-Host "   - PID=$procId" -ForegroundColor DarkGray
    }
  }

  # 非交互模式（带 action 参数）下：避免 Read-Host 阻塞，默认直接停止
  if ($Action -eq '') {
    $confirm = Read-Host "  确认停止？(Y/N，默认 Y)"
    if ($confirm -and ($confirm.ToUpperInvariant() -ne 'Y')) {
      Write-Host "  [i] 已取消" -ForegroundColor DarkYellow
      Write-Host ""
      return
    }
  }

  foreach ($procId in $pids) {
    try {
      Stop-Process -Id $procId -Force -ErrorAction Stop
      Write-Host "  [√] 已结束 PID=$procId" -ForegroundColor Green
    } catch {
      Write-Host "  [!] 结束 PID=$procId 失败（可能已退出）" -ForegroundColor DarkYellow
    }
  }
  Write-Host ""
}

function Show-Status {
  Write-Host ""
  Write-SectionHeader -title "模拟器状态" -lineColor DarkCyan -titleColor Cyan
  Write-Host ""

  $pids = Get-SimPids
  if ($pids.Count -eq 0) {
    Write-Host "  当前状态: ○ 已停止" -ForegroundColor DarkGray
    Write-Host ""
    return
  }

  Write-Host "  当前状态: ● 运行中" -ForegroundColor Green
  Write-Host ""

  foreach ($procId in $pids) {
    $cmd = Get-SimCommandLine $procId
    Write-Host "  - PID=$procId" -NoNewline -ForegroundColor White
    if ($cmd) { Write-Host "  sim.py" -ForegroundColor DarkGray } else { Write-Host "" }

    if ($cmd) {
      # 尝试从命令行提取目标服务器信息（--server 或 --host/--port）
      $server = $null
      if ($cmd -match '--server\s+([^\s"]+|"[^"]+")') {
        $server = $Matches[1].Trim('"')
      } elseif ($cmd -match '--host\s+([^\s"]+|"[^"]+")') {
        $h = $Matches[1].Trim('"')
        $p = '5000'
        if ($cmd -match '--port\s+(\d+)') { $p = $Matches[1] }
        $server = "http://$h`:$p"
      } else {
        $server = '自动(本机5000/5002兜底)'
      }
      Write-Host "    目标服务器: $server" -ForegroundColor Cyan
    }
  }

  $lanIps = Get-LanIPv4List
  Write-Host ""
  if ($lanIps.Count -gt 0) {
    Write-Host "  本机局域网 IPv4:" -ForegroundColor White
    foreach ($ip in $lanIps) { Write-Host "    - $ip" -ForegroundColor Cyan }
  } else {
    Write-Host "  本机局域网 IPv4: 未检测到可用地址" -ForegroundColor DarkYellow
  }
  Write-Host ""
}

function Is-Running {
  return ((Get-SimPids).Count -gt 0)
}

function Toggle-Sim {
  if (Is-Running) { Stop-Sim } else { Start-Sim }
}

function Show-Banner {
  Clear-Host
  Write-Host ""
  $inner = Get-UiInnerWidth
  $ui = Get-UiChars

  $title = 'EdgeWind 模拟器控制台'
  $title = Truncate-ToWidth $title $inner
  $titleW = Get-DisplayWidth $title
  if ($titleW -gt $inner) { $titleW = $inner }

  $leftPad = [int][Math]::Floor(($inner - $titleW) / 2)
  $rightPad = [int]($inner - $titleW - $leftPad)

  Write-Host ($ui.TL + ($ui.H * $inner) + $ui.TR) -ForegroundColor Magenta
  Write-Host ($ui.V + (' ' * $inner) + $ui.V) -ForegroundColor Magenta

  # 标题行（分段上色）：边框/填充 Magenta，标题 White
  Write-Host $ui.V -NoNewline -ForegroundColor Magenta
  Write-Host (' ' * $leftPad) -NoNewline -ForegroundColor Magenta
  Write-Host $title -NoNewline -ForegroundColor White
  Write-Host (' ' * $rightPad) -NoNewline -ForegroundColor Magenta
  Write-Host $ui.V -ForegroundColor Magenta

  Write-Host ($ui.V + (' ' * $inner) + $ui.V) -ForegroundColor Magenta
  Write-Host ($ui.BL + ($ui.H * $inner) + $ui.BR) -ForegroundColor Magenta
  Write-Host ""
}

function Menu {
  while ($true) {
    Show-Banner

    $running = Is-Running
    if ($running) {
      Write-Host "  当前状态: " -NoNewline
      Write-Host "● 运行中" -ForegroundColor Green
    } else {
      Write-Host "  当前状态: " -NoNewline
      Write-Host "○ 已停止" -ForegroundColor DarkGray
    }

    Write-Host ""
    $inner = Get-UiInnerWidth
    Write-Separator -innerWidth $inner -color DarkGray
    Write-Host ""
    Write-Host "  [S] 启动模拟器（新窗口可交互）" -ForegroundColor Green
    Write-Host "  [X] 停止模拟器（结束 sim.py）" -ForegroundColor Red
    Write-Host "  [R] 重启模拟器" -ForegroundColor Yellow
    Write-Host "  [T] 一键切换 (运行→停止 / 停止→启动)" -ForegroundColor Cyan
    Write-Host "  [I] 查看状态" -ForegroundColor Blue
    Write-Host "  [Q] 退出" -ForegroundColor DarkGray
    Write-Host ""
    Write-Separator -innerWidth $inner -color DarkGray
    Write-Host ""

    $sel = Read-Host "  请选择"
    switch ($sel.ToUpperInvariant()) {
      'S' { Start-Sim; Read-Host "  按回车继续" | Out-Null }
      'X' { Stop-Sim; Read-Host "  按回车继续" | Out-Null }
      'R' { Stop-Sim; Start-Sim; Read-Host "  按回车继续" | Out-Null }
      'T' { Toggle-Sim; Read-Host "  按回车继续" | Out-Null }
      'I' { Show-Status; Read-Host "  按回车继续" | Out-Null }
      'Q' {
        Write-Host ""
        Write-Host "  再见！" -ForegroundColor Magenta
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

switch ($Action) {
  'start'   { Start-Sim }
  'stop'    { Stop-Sim }
  'restart' { Stop-Sim; Start-Sim }
  'status'  { Show-Status }
  'toggle'  { Toggle-Sim }
  default   { Menu }
}
