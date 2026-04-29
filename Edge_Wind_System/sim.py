"""
模拟STM32H7硬件节点 - 专业交互式故障注入器 + 心跳发现系统
支持通过控制台命令手动触发故障和在线/离线控制，模拟边缘节点物理故障
同时通过后台线程发送心跳数据到Web界面
"""
import requests
import time
import random
import math
import json
import threading
import sys
import os
import argparse 

# Windows 控制台默认编码常为 GBK，遇到 emoji 等字符时可能触发 UnicodeEncodeError。
# 这里统一把 stdout/stderr 设置为 UTF-8 并用 replace 兜底，避免 sim.py 直接崩溃。
if sys.platform.startswith("win"):
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        # 旧版本/特殊环境不支持 reconfigure 时，忽略即可
        pass

# ======================== 服务器地址配置（重要） ========================
# 说明：之前这里写死了局域网地址（例如 10.xx.xx.xx:5000），一旦服务器换到本机
# 或端口自动切换到 5002，sim.py 就会“看起来像坏了”。
# 现在改为：
# - 默认连接本机：127.0.0.1:5000
# - 支持环境变量覆盖：EDGEWIND_SERVER_URL（例如 http://192.168.1.10:5002）
# - 支持命令行参数覆盖：--server / --host / --port
# - 当连接失败且未显式指定地址时，会自动尝试本机 5000/5002

_ENV_SERVER_URL_KEYS = ["EDGEWIND_SERVER_URL", "EDGEWIND_SERVER", "SERVER_URL"]
_ENV_DEVICE_API_KEY_KEYS = ["EDGEWIND_DEVICE_API_KEY", "DEVICE_API_KEY"]

def _get_device_headers():
    """
    设备侧鉴权请求头（与后端 EDGEWIND_DEVICE_API_KEY 对应）。
    - 未配置时返回空 dict，保持兼容。
    """
    for k in _ENV_DEVICE_API_KEY_KEYS:
        v = os.environ.get(k)
        if v and isinstance(v, str) and v.strip():
            return {"X-EdgeWind-ApiKey": v.strip()}
    return {}

def _get_env_server_url():
    """从环境变量读取服务器地址（优先级从左到右）。"""
    for k in _ENV_SERVER_URL_KEYS:
        val = os.environ.get(k)
        if val and isinstance(val, str) and val.strip():
            return val.strip().rstrip("/"), True
    return "http://127.0.0.1:5000", False

SERVER_URL, _SERVER_URL_EXPLICIT = _get_env_server_url()
HEARTBEAT_ENDPOINT = ""

def _rebuild_endpoints():
    """根据 SERVER_URL 重新构建各类端点（避免地址变更后端点仍指向旧地址）。"""
    global HEARTBEAT_ENDPOINT
    HEARTBEAT_ENDPOINT = f"{SERVER_URL}/api/node/heartbeat"

def apply_server_config_from_cli():
    """从命令行参数覆盖服务器地址（不会影响其他业务参数解析）。"""
    global SERVER_URL, _SERVER_URL_EXPLICIT

    # 使用 parse_known_args，避免和后续自定义命令/参数冲突
    parser = argparse.ArgumentParser(description="EdgeWind 模拟节点(sim.py) 参数", add_help=True)
    parser.add_argument("--server", type=str, default=None, help="服务器地址，例如 http://localhost:5000")
    parser.add_argument("--host", type=str, default=None, help="服务器主机，例如 127.0.0.1")
    parser.add_argument("--port", type=int, default=None, help="服务器端口，例如 5000 或 5002")
    args, _unknown = parser.parse_known_args()

    if args.server:
        SERVER_URL = args.server.strip().rstrip("/")
        _SERVER_URL_EXPLICIT = True
    elif args.host or args.port:
        host = (args.host or "127.0.0.1").strip()
        port = args.port or 5000
        SERVER_URL = f"http://{host}:{port}"
        _SERVER_URL_EXPLICIT = True

    _rebuild_endpoints()

def _try_local_fallback_urls():
    """当未显式指定 SERVER_URL 且连接失败时，自动尝试本机常用端口。"""
    # 保持顺序：先 5000 再 5002（与 edgewind_ctl 默认行为一致）
    candidates = [
        "http://127.0.0.1:5000",
        "http://127.0.0.1:5002",
        "http://localhost:5000",
        "http://localhost:5002",
    ]
    # 去重 + 排除当前地址
    seen = set()
    out = []
    for u in candidates:
        if u == SERVER_URL:
            continue
        if u in seen:
            continue
        seen.add(u)
        out.append(u)
    return out

_rebuild_endpoints()

# 故障代码: E00=Normal, E01=AC Intrusion, E02=Insulation Fault, E03=DC Capacitor Aging, E04=IGBT Open Circuit, E05=DC Bus Grounding
FAULT_CODES = ["E00", "E01", "E02", "E03", "E04", "E05"]

# 全局节点管理（线程安全）
NODES = {}  # 存储节点实例 {node_index: SimulatedHardwareNode}
NODES_LOCK = threading.Lock()

# 全局节点配置列表（用于心跳发现系统，包含完整通道元数据）
# 这个列表会被心跳线程和主线程共享访问，使用锁保护
NODES_CONFIG = []  # 动态节点列表，初始为空

# 全局噪声级别（模拟环境干扰，影响所有节点）
GLOBAL_NOISE_LEVEL = 0.1  # 默认噪声级别（可调整）

# 节点配置锁（保护 NODES_CONFIG 的并发访问）
CONFIG_LOCK = threading.Lock()

def create_node_template(node_id):
    """
    创建标准节点配置模板
    
    Args:
        node_id: 节点ID（如 "STM32_Node_001"）
    
    Returns:
        dict: 标准节点配置字典
    """
    return {
        "node_id": node_id,
        "status": "online",  # online 或 fault
        "fault_code": "E00",  # 故障代码：E00=正常, E01-E05=故障
        "channels": [
            {"id": 0, "label": "直流母线(+)", "unit": "V", "type": "DC", "range": [0, 800], "variation": 2, "color": "primary"},
            {"id": 1, "label": "直流母线(-)", "unit": "V", "type": "DC", "range": [-800, 0], "variation": 2, "color": "danger"},
            {"id": 2, "label": "负载电流", "unit": "A", "type": "Current", "range": [0, 60], "variation": 1, "color": "info"},
            {"id": 3, "label": "漏电流", "unit": "mA", "type": "Leakage", "range": [0, 50], "variation": 0.01, "color": "warning"}
        ]
    }

# 故障代码名称映射
FAULT_NAMES = {
    "E00": "正常",
    "E01": "交流窜入",
    "E02": "绝缘故障",
    "E03": "直流母线电容老化",
    "E04": "变流器IGBT开路",
    "E05": "直流母线接地故障"
}

class SimulatedHardwareNode:
    """模拟的STM32硬件节点（单线程）"""
    
    def __init__(self, device_id, location, node_index, base_voltage=220.0):
        self.device_id = device_id
        self.location = location
        self.node_index = node_index  # 节点索引 (1-5)
        self.base_voltage = base_voltage
        self.status = "normal"
        self.fault_code = "E00"
        self.is_online = True  # 节点在线状态
        self.running = True
        self.registered = False  # 是否已注册到服务器
        
        # 状态变化追踪（用于日志控制）
        self.last_fault_code = "E00"
        self.last_online_status = True
        
    def generate_waveform(self, samples=1024):
        """
        生成模拟的ADC波形数据（1024个浮点数数组）
        基于当前故障代码生成对应的波形特征
        
        Args:
            samples: 采样点数（默认1024）
        """
        waveform = []
        
        for i in range(samples):
            t = i * 0.001  # 假设采样率为1kHz，每个点1ms
            
            if self.fault_code == "E00":  # Normal
                # 正常模式：220V DC + 小幅度噪声
                voltage = self.base_voltage + 5.0 * math.sin(2 * math.pi * 50 * t) + random.gauss(0, 0.5)
            elif self.fault_code == "E01":  # AC Intrusion
                # 交流窜入：出现大幅度的50Hz交流信号 + 高频谐波
                voltage = self.base_voltage + 20.0 * math.sin(2 * math.pi * 50 * t) + \
                          3.0 * math.sin(2 * math.pi * 150 * t) + random.gauss(0, 1.0)
            elif self.fault_code == "E02":  # Insulation Fault
                # 绝缘故障：出现不规则的脉冲和电压下降
                voltage = self.base_voltage - 30.0 + \
                          (random.random() - 0.5) * 15.0 * math.sin(2 * math.pi * 50 * t) + \
                          random.gauss(0, 2.0)
            elif self.fault_code == "E03":  # DC Capacitor Aging
                # 直流母线电容老化：叠加大量高频纹波噪声（模拟滤波能力下降）
                base_signal = self.base_voltage + 5.0 * math.sin(2 * math.pi * 50 * t)
                # 叠加高频纹波（10kHz-100kHz范围的多频率成分）
                high_freq_ripple = 15.0 * math.sin(2 * math.pi * 10000 * t) + \
                                   8.0 * math.sin(2 * math.pi * 25000 * t) + \
                                   5.0 * math.sin(2 * math.pi * 50000 * t) + \
                                   random.gauss(0, 3.0)  # 额外的随机噪声
                voltage = base_signal + high_freq_ripple
            elif self.fault_code == "E04":  # IGBT Open Circuit
                # IGBT开路：正弦波的一半周期被削平（半波整流效果）
                sine_wave = 20.0 * math.sin(2 * math.pi * 50 * t)
                # 如果正弦波为负值或接近零，则置零（模拟缺失的开关相位）
                if sine_wave < 0:
                    sine_wave = 0
                voltage = self.base_voltage + sine_wave + random.gauss(0, 0.5)
            elif self.fault_code == "E05":  # DC Bus Grounding
                # 直流母线接地故障：大幅度的DC偏移（向下偏移-200V）
                voltage = self.base_voltage - 200.0 + 5.0 * math.sin(2 * math.pi * 50 * t) + random.gauss(0, 0.5)
            else:
                voltage = self.base_voltage + random.gauss(0, 0.5)
            
            # 限制数值范围（对于接地故障E05，允许更大的负偏移范围）
            if self.fault_code == "E05":
                # 接地故障可能造成较大的负偏移，放宽限制范围
                voltage = max(0.0, min(250.0, voltage))
            else:
                # 其他故障类型的正常限制
                voltage = max(180.0, min(250.0, voltage))
            waveform.append(round(voltage, 2))
        
        return waveform
    
    def register(self):
        """注册设备到服务器"""
        try:
            response = requests.post(
                f"{SERVER_URL}/api/register",
                headers=_get_device_headers(),
                json={
                    "device_id": self.device_id,
                    "location": self.location,
                    "hw_version": "v1.0"
                },
                timeout=2  # 注册操作使用2秒超时（非高频操作）
            )
            
            if response.status_code in [200, 201]:
                self.registered = True
                print(f"[节点 #{self.node_index}] ✅ 已注册: {self.device_id}")
                return True
            else:
                print(f"[节点 #{self.node_index}] ❌ 注册失败: {response.text}")
                return False
                
        except requests.exceptions.RequestException as e:
            print(f"[节点 #{self.node_index}] ❌ 注册失败 (连接错误): {e}")
            return False
    
    def upload_data(self):
        """上传数据和心跳（仅在在线时执行）"""
        if not self.is_online:
            return False  # 离线状态不发送数据
            
        try:
            # 生成波形数据（1024个浮点数）
            waveform = self.generate_waveform(samples=1024)
            
            # 构建上传数据（不包含时间戳，由服务器生成）
            upload_data = {
                "device_id": self.device_id,
                "status": self.status,
                "fault_code": self.fault_code,
                "waveform": waveform
            }
            
            # 发送数据
            response = requests.post(
                f"{SERVER_URL}/api/upload",
                headers=_get_device_headers(),
                json=upload_data,
                timeout=0.2  # 缩短超时时间，提高响应速度（200ms）
            )
            
            # 解析响应，检查是否有服务器端复位命令
            if response.status_code == 200:
                try:
                    resp_data = response.json()
                    # 支持两种命令格式：reset 和 reset_local_state（向后兼容）
                    command = resp_data.get("command")
                    if command == "reset" or command == "reset_local_state":
                        # 收到服务器复位指令，自动恢复正常状态
                        print(f"[节点 #{self.node_index}] 🔄 收到服务器复位指令 ({command})，自动恢复正常状态...")
                        self.fault_code = "E00"
                        self.status = "normal"
                        # 更新状态变化追踪
                        self.last_fault_code = "E00"
                        print(f"[节点 #{self.node_index}] ✅ 状态已重置为正常 (E00)")
                        return True
                except (ValueError, KeyError):
                    # 响应不是JSON或没有command字段，继续正常流程
                    pass
                
                return True
            else:
                return False
                
        except requests.exceptions.RequestException:
            # 静默处理网络错误（避免日志刷屏）
            return False
    
    def check_status_changes(self):
        """检查状态变化并打印日志（仅在状态变化时）"""
        # 检查在线状态变化
        if self.is_online != self.last_online_status:
            if self.is_online:
                print(f"[节点 #{self.node_index}] 🟢 已上线: {self.device_id}")
            else:
                print(f"[节点 #{self.node_index}] 🔴 已离线: {self.device_id}")
            self.last_online_status = self.is_online
        
        # 检查故障代码变化
        if self.fault_code != self.last_fault_code:
            old_name = FAULT_NAMES.get(self.last_fault_code, self.last_fault_code)
            new_name = FAULT_NAMES.get(self.fault_code, self.fault_code)
            
            if self.fault_code == "E00":
                print(f"[节点 #{self.node_index}] ✅ 状态变更: {old_name} -> {new_name}")
            else:
                print(f"[节点 #{self.node_index}] ⚠️  故障触发: {new_name} ({self.fault_code})")
            
            self.last_fault_code = self.fault_code
            self.status = "normal" if self.fault_code == "E00" else "fault"
            
            # 故障变化时立即上传数据（快速响应）
            if self.is_online:
                self.upload_data()
    
    def run(self, interval=3):
        """运行节点循环（在线程中执行）"""
        # 首先注册
        if not self.register():
            print(f"[节点 #{self.node_index}] ❌ 注册失败，节点退出")
            return
        
        # 循环上传数据
        try:
            while self.running:
                # 从配置同步故障代码（快速响应控制台命令）
                with NODES_LOCK:
                    config = NODES_CONFIG.get(self.node_index - 1)
                    if config:
                        new_fault_code = config.get("fault_code", "E00")
                        if new_fault_code != self.fault_code:
                            self.fault_code = new_fault_code
                            self.status = "normal" if self.fault_code == "E00" else "fault"
                
                # 检查状态变化（仅在变化时打印日志）
                self.check_status_changes()
                
                # 仅在在线时上传数据
                if self.is_online:
                    self.upload_data()
                
                time.sleep(interval)
                
        except KeyboardInterrupt:
            print(f"[节点 #{self.node_index}] ⏹️  节点停止")
        except Exception as e:
            print(f"[节点 #{self.node_index}] ❌ 运行时错误: {e}")

def console_menu():
    """控制台命令菜单（在单独线程中运行）"""
    print("\n" + "=" * 70)
    print("📋 控制台故障注入器已启动")
    print("=" * 70)
    print_help()
    print("=" * 70 + "\n")
    
    while True:
        try:
            # 获取用户输入（不转换为小写，保持原始大小写以支持设备ID）
            command = input("🚀 指令 > ").strip()
            # 将命令转换为小写用于匹配（但保留原始参数）
            command_lower = command.lower()
            
            if not command:
                continue
            
            # 解析命令（保持原始大小写）
            parts = command.split()
            # 用于命令匹配的小写版本
            parts_lower = [p.lower() for p in parts]
            
            if len(parts) == 0:
                continue
            
            # 处理特殊命令（使用小写匹配）
            if parts_lower[0] == "help":
                print_help()
                continue
            elif parts_lower[0] == "status":
                print_status_table()
                continue
            elif parts_lower[0] == "list":
                # 简化命令：显示所有节点状态
                print_status_table()
                continue
            elif parts_lower[0] == "exit" or parts_lower[0] == "quit":
                print("\n[控制台] 👋 退出控制台菜单（节点将继续运行）")
                print("按 Ctrl+C 可完全停止程序")
                break
            elif parts_lower[0] == "add":
                # 动态注册节点：add <device_id> <location>
                # 支持两种格式：
                # 1. add <device_id> <location> - 完整格式，指定设备ID和位置
                # 2. add <number> - 简化格式，自动生成设备ID，提示输入位置
                if len(parts) >= 2:
                    device_id = None
                    location = None
                    
                    # 优化：自动去除 < > 符号（如果用户误输入）
                    raw_device_id = parts[1].strip().strip('<>').strip()
                    raw_location = " ".join(parts[2:]).strip().strip('<>').strip() if len(parts) >= 3 else None
                    
                    # 判断是数字还是设备ID
                    try:
                        node_num = int(raw_device_id)
                        # 如果是数字，使用简化格式
                        if node_num < 1:
                            print("[控制台] ❌ 错误: 节点编号必须 >= 1")
                            continue
                        device_id = f"STM32_Node_{node_num:03d}"
                        
                        # 处理位置
                        if raw_location:
                            location = raw_location
                        else:
                            print(f"[控制台] 💡 请输入节点位置（设备ID: {device_id}）")
                            location_input = input("   位置: ").strip().strip('<>').strip()
                            if not location_input:
                                print("[控制台] ❌ 错误: 位置不能为空")
                                continue
                            location = location_input
                    except ValueError:
                        # 如果不是数字，当作设备ID处理
                        device_id = raw_device_id
                        
                        # 处理位置
                        if raw_location:
                            location = raw_location
                        else:
                            print(f"[控制台] 💡 请输入节点位置（设备ID: {device_id}）")
                            location_input = input("   位置: ").strip().strip('<>').strip()
                            if not location_input:
                                print("[控制台] ❌ 错误: 位置不能为空")
                                continue
                            location = location_input
                    
                    # 验证设备ID和位置
                    if not device_id or not device_id.strip():
                        print("[控制台] ❌ 错误: 设备ID不能为空")
                        continue
                    if not location or not location.strip():
                        print("[控制台] ❌ 错误: 位置不能为空")
                        continue
                    
                    device_id = device_id.strip()
                    location = location.strip()
                    
                    # 检查节点是否已存在
                    with CONFIG_LOCK:
                        existing = any(nc["node_id"] == device_id for nc in NODES_CONFIG)
                        
                        if existing:
                            print(f"[控制台] ⚠️  节点 {device_id} 已存在，忽略")
                        else:
                            # 创建新节点配置
                            new_node = create_node_template(device_id)
                            # 添加位置信息到配置中（用于后续创建节点实例）
                            new_node["location"] = location
                            NODES_CONFIG.append(new_node)
                            print(f"[控制台] ✅ 节点已注册并开始发送心跳")
                            print(f"       设备ID: {device_id}")
                            print(f"       位置: {location}")
                            print(f"       状态: {new_node['status']} (故障代码: {new_node['fault_code']})")
                else:
                    print("[控制台] ❌ 错误: 用法: add <device_id> <location>")
                    print("   或: add <number> [位置]")
                    print("   示例:")
                    print("     add STM32_Node_001 风机#1直流母线")
                    print("     add 6 风机#6直流母线")
                    print("     add 6  (会提示输入位置)")
                continue
            elif parts_lower[0] == "remove":
                # 动态注销节点：remove <device_id或number>（支持设备ID或数字编号）
                if len(parts) >= 2:
                    node_identifier = parts[1].strip()
                    node_id = None
                    
                    # 判断是数字还是设备ID
                    try:
                        node_num = int(node_identifier)
                        # 如果是数字，转换为标准设备ID格式
                        if node_num < 1:
                            print("[控制台] ❌ 错误: 节点编号必须 >= 1")
                            continue
                        node_id = f"STM32_Node_{node_num:03d}"
                    except ValueError:
                        # 如果不是数字，直接使用作为设备ID
                        node_id = node_identifier
                    
                    # 从 NODES_CONFIG 中移除
                    removed = False
                    with CONFIG_LOCK:
                        original_count = len(NODES_CONFIG)
                        NODES_CONFIG[:] = [nc for nc in NODES_CONFIG if nc["node_id"] != node_id]
                        removed = original_count > len(NODES_CONFIG)
                    
                    if removed:
                        print(f"[控制台] ✅ 节点 {node_id} 已注销，心跳已停止")
                        print(f"       节点将在10秒后从Web界面消失（超时机制）")
                    else:
                        print(f"[控制台] ⚠️  节点 '{node_id}' 不存在")
                else:
                    print("[控制台] ❌ 错误: 用法: remove <device_id或number>")
                    print("   示例: remove 5   -> 注销 STM32_Node_005")
                    print("   示例: remove STM32_Node_001  -> 注销 STM32_Node_001")
                    print("   示例: remove 1k  -> 注销 1k")
                continue
            elif parts_lower[0] == "fault":
                # 故障注入命令：fault <device_id或number> [E01-E05] -> 注入故障（支持设备ID或数字编号）
                if len(parts) >= 2:
                    node_identifier = parts[1].strip()
                    node_id = None
                    
                    # 判断是数字还是设备ID
                    try:
                        node_num = int(node_identifier)
                        # 如果是数字，转换为标准设备ID格式
                        if node_num < 1:
                            print("[控制台] ❌ 错误: 节点编号必须 >= 1")
                            continue
                        node_id = f"STM32_Node_{node_num:03d}"
                    except ValueError:
                        # 如果不是数字，直接使用作为设备ID
                        node_id = node_identifier
                    
                    # 检查节点是否存在
                    with CONFIG_LOCK:
                        config_index = None
                        for idx, nc in enumerate(NODES_CONFIG):
                            if nc["node_id"] == node_id:
                                config_index = idx
                                break
                        
                        if config_index is None:
                            print(f"[控制台] ❌ 错误: 节点 '{node_id}' 不存在，请先使用 'add' 命令注册")
                            continue
                        
                        # 如果提供了故障代码，使用它；否则默认 E01
                        if len(parts) >= 3:
                            fault_code = parse_fault_code(parts[2])
                            if fault_code is None or fault_code == "E00":
                                print(f"[控制台] ❌ 错误: 无效的故障代码 '{parts[2]}' (应为 E01-E05)")
                                continue
                            NODES_CONFIG[config_index]["fault_code"] = fault_code
                            NODES_CONFIG[config_index]["status"] = "fault"
                        else:
                            # 默认注入 E01 (交流窜入) 故障
                            NODES_CONFIG[config_index]["fault_code"] = "E01"
                            NODES_CONFIG[config_index]["status"] = "fault"
                        
                        fault_code = NODES_CONFIG[config_index]["fault_code"]
                        fault_name = FAULT_NAMES.get(fault_code, fault_code)
                        print(f"[控制台] ✅ 节点 {node_id} 已切换为: {fault_name} ({fault_code})")
                else:
                    print("[控制台] ❌ 错误: 用法: fault <device_id或number> [E01-E05]")
                    print("   示例: fault 1      -> 节点1注入E01故障")
                    print("   示例: fault 6 E04  -> 节点6注入E04故障")
                    print("   示例: fault STM32_Node_001 E02  -> 节点STM32_Node_001注入E02故障")
                    print("   示例: fault 1k E01  -> 节点1k注入E01故障")
                continue
            elif parts_lower[0] == "clear":
                # 清除故障命令：clear <device_id或number> -> 清除故障，强制恢复健康状态（支持设备ID或数字编号）
                if len(parts) >= 2:
                    node_identifier = parts[1].strip()
                    node_id = None
                    
                    # 判断是数字还是设备ID
                    try:
                        node_num = int(node_identifier)
                        # 如果是数字，转换为标准设备ID格式
                        if node_num < 1:
                            print(f"[控制台] ❌ 错误: 节点编号必须 >= 1")
                            continue
                        node_id = f"STM32_Node_{node_num:03d}"
                    except ValueError:
                        # 如果不是数字，直接使用作为设备ID
                        node_id = node_identifier
                    
                    # 检查节点是否存在并重置
                    with CONFIG_LOCK:
                        config_index = None
                        for idx, nc in enumerate(NODES_CONFIG):
                            if nc["node_id"] == node_id:
                                config_index = idx
                                break
                        
                        if config_index is None:
                            print(f"[控制台] ❌ 错误: 节点 '{node_id}' 不存在")
                            continue
                        
                        # 重置节点状态
                        NODES_CONFIG[config_index]["status"] = "online"
                        NODES_CONFIG[config_index]["fault_code"] = "E00"
                        
                        print(f"[控制台] ✅ 节点 {node_id} 已清除故障，恢复正常状态")
                        print(f"       心跳状态已同步: online (故障代码: E00)")
                else:
                    print("[控制台] ❌ 错误: 用法: clear <device_id或number>")
                    print("   示例: clear 1   -> 清除节点1故障")
                    print("   示例: clear 6    -> 清除节点6故障")
                    print("   示例: clear STM32_Node_001  -> 清除节点STM32_Node_001故障")
                    print("   示例: clear 1k  -> 清除节点1k故障")
                continue
            
            # 处理故障和状态控制命令: [id] [command]（仅对已存在的节点）
            # 支持格式: <device_id或number> <command>，例如: 1k e01, STM32_Node_001 e02, 集房子 e01
            if len(parts) >= 2:
                node_arg = parts[0]  # 保持原始大小写
                command_arg = parts[1].lower()  # 命令转换为小写用于匹配
                
                # 处理批量命令 "all"
                if node_arg.lower() == "all":
                    if command_arg in ["normal", "e00", "e0"]:
                        set_all_nodes_normal()
                    elif command_arg == "on":
                        set_all_nodes_online(True)
                    elif command_arg == "off":
                        set_all_nodes_online(False)
                    else:
                        print("[控制台] ❌ 错误: 'all' 命令仅支持 'normal', 'on', 'off'")
                    continue
                
                # 解析节点标识符（支持数字编号或设备ID）
                node_id = None
                try:
                    node_num = int(node_arg)
                    # 如果是数字，转换为标准设备ID格式
                    if node_num < 1:
                        print(f"[控制台] ❌ 错误: 节点编号必须 >= 1")
                        continue
                    node_id = f"STM32_Node_{node_num:03d}"
                except ValueError:
                    # 如果不是数字，直接使用作为设备ID（保持原始大小写）
                    node_id = node_arg
                
                # 检查节点是否存在（使用精确匹配，区分大小写）
                with CONFIG_LOCK:
                    config_index = None
                    for idx, nc in enumerate(NODES_CONFIG):
                        if nc["node_id"] == node_id:
                            config_index = idx
                            break
                    
                    if config_index is None:
                        print(f"[控制台] ❌ 错误: 节点 '{node_id}' 不存在，请先使用 'add' 命令注册")
                        print(f"   提示: 使用 'list' 命令查看所有已注册的节点")
                        continue
                
                # 处理在线/离线命令（这些命令现在通过 NODES_CONFIG 管理）
                if command_arg == "on":
                    with CONFIG_LOCK:
                        NODES_CONFIG[config_index]["status"] = "online"
                    print(f"[控制台] ✅ 节点 {node_id} 已设置为在线")
                elif command_arg == "off":
                    with CONFIG_LOCK:
                        NODES_CONFIG[config_index]["status"] = "offline"
                    print(f"[控制台] ✅ 节点 {node_id} 已设置为离线")
                # 处理故障代码命令
                elif command_arg == "normal" or command_arg == "e00" or command_arg == "e0":
                    with CONFIG_LOCK:
                        NODES_CONFIG[config_index]["status"] = "online"
                        NODES_CONFIG[config_index]["fault_code"] = "E00"
                    print(f"[控制台] ✅ 节点 {node_id} 已重置为正常状态")
                else:
                    # 解析故障代码（支持大小写不敏感）
                    fault_code = parse_fault_code(parts[1])  # 使用原始输入，parse_fault_code会处理大小写
                    if fault_code is None:
                        print(f"[控制台] ❌ 错误: 无效的命令或故障代码 '{parts[1]}'")
                        print("   有效的命令: normal/e00, e01-e05, on, off")
                        continue
                    
                    with CONFIG_LOCK:
                        if fault_code == "E00":
                            NODES_CONFIG[config_index]["status"] = "online"
                            NODES_CONFIG[config_index]["fault_code"] = "E00"
                        else:
                            NODES_CONFIG[config_index]["status"] = "fault"
                            NODES_CONFIG[config_index]["fault_code"] = fault_code
                    
                    fault_name = FAULT_NAMES.get(fault_code, fault_code)
                    print(f"[控制台] ✅ 节点 {node_id} 已切换为: {fault_name} ({fault_code})")
            else:
                print("[控制台] ❌ 错误: 命令格式不正确")
                print("   格式: [节点编号] [命令] 或输入 'help' 查看帮助")
                
        except EOFError:
            # Ctrl+D (Unix) 或 Ctrl+Z (Windows)
            print("\n[控制台] 👋 退出控制台菜单")
            break
        except KeyboardInterrupt:
            # Ctrl+C 不会退出控制台，只清除当前输入
            print("\n[控制台] 💡 提示: 按 'exit' 退出控制台菜单，Ctrl+C 停止程序")
        except Exception as e:
            print(f"[控制台] ❌ 错误: {e}")

def parse_fault_code(fault_arg):
    """解析故障代码字符串"""
    fault_arg = fault_arg.upper().strip()
    
    # 映射表
    mapping = {
        "NORMAL": "E00",
        "E00": "E00",
        "E0": "E00",
        "E01": "E01",
        "E02": "E02",
        "E03": "E03",
        "E04": "E04",
        "E05": "E05"
    }
    
    return mapping.get(fault_arg)

def set_node_fault(node_index, fault_code):
    """设置指定节点的故障状态（使用节点编号，兼容旧代码）"""
    # 查找对应的节点ID
    node_id = f"STM32_Node_{node_index:03d}"
    
    with CONFIG_LOCK:
        config_index = None
        for idx, nc in enumerate(NODES_CONFIG):
            if nc["node_id"] == node_id:
                config_index = idx
                break
        
        if config_index is None:
            print(f"[控制台] ❌ 错误: 节点 {node_id} 不存在，请先使用 'add {node_index}' 注册")
            return
        
        # 更新 NODES_CONFIG 中的状态
        if fault_code == "E00":
            NODES_CONFIG[config_index]["status"] = "online"
            NODES_CONFIG[config_index]["fault_code"] = "E00"
        else:
            NODES_CONFIG[config_index]["status"] = "fault"
            NODES_CONFIG[config_index]["fault_code"] = fault_code
        
        # 打印确认信息
        fault_name = FAULT_NAMES.get(fault_code, fault_code)
        print(f"[成功] 节点 {node_id} 已切换为: {fault_name} ({fault_code})")
        print(f"       心跳状态已同步: {NODES_CONFIG[config_index]['status']} (故障代码: {NODES_CONFIG[config_index]['fault_code']})")

def set_node_online(node_index, online):
    """设置指定节点的在线状态（使用节点编号，兼容旧代码）"""
    # 查找对应的节点ID
    node_id = f"STM32_Node_{node_index:03d}"
    
    with CONFIG_LOCK:
        config_index = None
        for idx, nc in enumerate(NODES_CONFIG):
            if nc["node_id"] == node_id:
                config_index = idx
                break
        
        if config_index is None:
            print(f"[控制台] ❌ 错误: 节点 {node_id} 不存在，请先使用 'add {node_index}' 注册")
            return
        
        # 更新状态
        NODES_CONFIG[config_index]["status"] = "online" if online else "offline"
        
        # 打印确认信息
        status_text = "在线模式 (Online)" if online else "离线模式 (Offline)"
        print(f"[成功] 节点 {node_id} 已切换为: {status_text}")

def set_all_nodes_normal():
    """重置所有节点为正常状态"""
    with CONFIG_LOCK:
        count = 0
        for node_config in NODES_CONFIG:
            if node_config.get("fault_code", "E00") != "E00":
                node_config["status"] = "online"
                node_config["fault_code"] = "E00"
                count += 1
        
        print(f"[成功] 已重置 {count} 个节点为正常状态")

def set_all_nodes_online(online):
    """批量设置所有节点的在线状态"""
    with CONFIG_LOCK:
        count = 0
        target_status = "online" if online else "offline"
        for node_config in NODES_CONFIG:
            current_status = node_config.get("status", "offline")
            if current_status != target_status:
                node_config["status"] = target_status
                count += 1
        
        status_text = "在线模式" if online else "离线模式"
        print(f"[成功] 已切换 {count} 个节点为 {status_text}")

def print_status_table():
    """打印所有已注册节点的状态表格（动态节点列表）"""
    with CONFIG_LOCK:
        print("\n" + "=" * 70)
        print("📊 节点状态总览（动态节点列表）")
        print("=" * 70)
        print(f"{'节点ID':<20} {'位置':<25} {'状态':<12} {'当前故障':<20} {'故障代码':<10}")
        print("-" * 100)
        
        if len(NODES_CONFIG) == 0:
            print("  (暂无已注册节点，使用 'add <device_id> <location>' 命令注册节点)")
        else:
            for node_config in sorted(NODES_CONFIG, key=lambda x: x["node_id"]):
                node_id = node_config["node_id"]
                status = node_config.get("status", "unknown")
                fault_code = node_config.get("fault_code", "E00")
                fault_name = FAULT_NAMES.get(fault_code, fault_code)
                
                status_icon = "🟢" if status == "online" else "🔴" if status == "fault" else "⚪"
                status_text = "在线" if status == "online" else "故障" if status == "fault" else "未知"
                fault_icon = "✅" if fault_code == "E00" else "⚠️"
                
                location = node_config.get("location", "N/A")
                location_display = location[:23] + "..." if len(location) > 25 else location
                print(f"{node_id:<20} {location_display:<25} {status_icon} {status_text:<9} {fault_icon} {fault_name:<17} {fault_code:<10}")
        
        print("=" * 70)
        print(f"总计: {len(NODES_CONFIG)} 个已注册节点\n")

def print_help():
    """打印帮助信息"""
    print("\n📖 可用命令:")
    print("\n  📌 简化命令 (推荐):")
    print("    add <device_id> <location>  - 动态注册新节点并开始心跳")
    print("      或: add <number> [location]  - 简化格式（自动生成设备ID）")
    print("                            示例: add STM32_Node_001 风机#1直流母线")
    print("                            示例: add 6 风机#6直流母线")
    print("    remove <device_id或number>  - 动态注销节点，停止心跳")
    print("                            示例: remove 5   -> 注销 STM32_Node_005")
    print("    fault <device_id或number> [E01-E05]  - 为节点注入故障 (默认: E01)")
    print("                            示例: fault 1      -> 节点1注入E01故障")
    print("                            示例: fault 1 E04  -> 节点1注入E04故障")
    print("    clear <device_id或number>  - 清除节点故障，恢复正常状态")
    print("    list                - 显示所有节点状态表格")
    print("\n  📌 状态控制 (详细):")
    print("    [节点索引] normal    - 设置节点为正常状态 (E00)")
    print("    [节点索引] e01-e05   - 为节点注入指定故障")
    print("    all normal           - 重置所有节点为正常状态")
    print("\n  📌 连接控制:")
    print("    [节点索引] on        - 设置节点为在线（开始发送数据）")
    print("    [节点索引] off       - 设置节点为离线（停止发送数据）")
    print("    all on               - 所有节点上线")
    print("    all off              - 所有节点下线")
    print("\n  📌 信息查询:")
    print("    status               - 显示所有节点状态表格")
    print("    help                 - 显示此帮助信息")
    print("    exit/quit            - 退出控制台菜单")
    print("\n📌 节点索引: 1-5 (对应 STM32_Node_001 到 STM32_Node_005)")
    print("\n📌 故障代码:")
    print("    e01  - 交流窜入")
    print("    e02  - 绝缘故障")
    print("    e03  - 直流母线电容老化")
    print("    e04  - 变流器IGBT开路")
    print("    e05  - 直流母线接地故障")
    print("\n💡 示例:")
    print("  fault 1               - 为节点1注入故障 (快速命令)")
    print("  clear 1               - 清除节点1故障 (快速命令)")
    print("  1 e01                 - 为节点1注入交流窜入故障 (详细命令)")
    print("  3 e03                 - 为节点3注入电容老化故障")
    print("  2 normal              - 将节点2重置为正常状态")
    print("  all normal            - 重置所有节点")
    print()

def create_and_run_node(device_id, location, node_index, base_voltage=220.0):
    """创建并运行一个节点（在线程中）
    
    Args:
        device_id: 设备ID字符串，如 "STM32_Node_001"
        location: 位置字符串，如 "Turbine_#1_DC_Bus"
        node_index: 节点索引 (1-5)
        base_voltage: 基础电压值（默认220.0V）
    """
    # 为每个节点设置略微不同的基础电压，模拟真实环境
    node_voltage = base_voltage + random.uniform(-5.0, 5.0)
    
    node = SimulatedHardwareNode(
        device_id=device_id,
        location=location,
        node_index=node_index,
        base_voltage=node_voltage
    )
    
    # 将节点注册到全局字典
    with NODES_LOCK:
        NODES[node_index] = node
    
    # 运行节点循环
    node.run(interval=0.02)  # 每0.02秒上传一次（20ms，50Hz更新频率，高频刷新）

# ========== 物理常数（工业标准）==========
FS = 5120.0       # 采样率：5.12 kHz
N_POINTS = 1024   # 采样点数：1024点（0.2秒快照）
FREQ_RES = FS / N_POINTS  # 频率分辨率：5 Hz/bin
NUM_FFT_BINS = 115  # 计算前115个bin（0-575Hz），覆盖550Hz范围（110 * 5 = 550Hz），Bin 10 = 50Hz

def compute_partial_dft(signal, num_bins=NUM_FFT_BINS):
    """
    计算真实DFT幅度（仅前num_bins个频率bin）
    分辨率 = 5Hz/bin
    Bin 0=0Hz, Bin 1=5Hz, ..., Bin 10=50Hz
    
    Args:
        signal: 时域信号数组（1024个点）
        num_bins: 要计算的频率bin数量（默认100，对应0-500Hz）
    
    Returns:
        list: DFT幅度数组（单位：V或A，已归一化）
    """
    magnitudes = []
    
    for k in range(num_bins):
        re = 0.0  # 实部
        im = 0.0  # 虚部
        
        # 标准DFT相关计算
        for n, val in enumerate(signal):
            angle = -2 * math.pi * k * n / N_POINTS
            re += val * math.cos(angle)
            im += val * math.sin(angle)
        
        # 幅度计算
        mag = math.sqrt(re**2 + im**2)
        
        # 归一化：DC分量用1/N，AC分量用2/N
        norm_factor = 1.0 / N_POINTS if k == 0 else 2.0 / N_POINTS
        magnitudes.append(round(mag * norm_factor, 2))
    
    return magnitudes

def generate_correlated_waveforms(fault_code, time_offset, global_noise=GLOBAL_NOISE_LEVEL):
    """
    生成具有物理关联性的4通道波形数据
    
    物理关联:
    1. 直流母线+和-电压互为镜像 (V+ ≈ -V-)
    2. 负载电流与电压功率相关 (I = P/V)
    3. 漏电流与绝缘故障相关
    4. 故障对所有通道同步影响
    
    Args:
        fault_code: 故障代码 (E00-E05)
        time_offset: 时间偏移
        global_noise: 全局噪声级别
    
    Returns:
        dict: {
            'voltage_pos': waveform,  # 直流母线(+)
            'voltage_neg': waveform,  # 直流母线(-)
            'current': waveform,      # 负载电流
            'leakage': waveform       # 漏电流
        }
    """
    current_time = time.time() + time_offset
    
    # 基础物理参数
    V_BASE = 375.0  # 基础电压(V)
    I_BASE = 12.0   # 基础电流(A)
    L_BASE = 0.02   # 基础漏电流(mA)
    
    # 初始化4个通道的波形数组
    voltage_pos = []
    voltage_neg = []
    current_wf = []
    leakage_wf = []
    
    # ========== 根据故障类型生成关联波形 ==========
    
    if fault_code == "E00":  # 正常状态
        for i in range(N_POINTS):
            t = i / FS
            t_with_offset = t + current_time * 0.1
            
            # 1. 直流母线电压(+): 非常稳定，微小纹波
            v_pos = V_BASE + 0.2 * math.sin(2 * math.pi * 100 * t_with_offset) + \
                    random.gauss(0, 0.5)
            
            # 2. 直流母线电压(-): 与(+)镜像，略有不对称
            v_neg = -V_BASE - 0.2 * math.sin(2 * math.pi * 100 * t_with_offset + 0.1) + \
                    random.gauss(0, 0.5)
            
            # 3. 负载电流: 与电压有功率关系 P = V * I (稳定功率)
            i_val = I_BASE + 0.15 * math.sin(2 * math.pi * 50 * t_with_offset) + \
                    random.gauss(0, 0.1)
            
            # 4. 漏电流: 正常状态非常小
            l_val = L_BASE + random.gauss(0, 0.005)
            
            voltage_pos.append(round(v_pos, 3))
            voltage_neg.append(round(v_neg, 3))
            current_wf.append(round(i_val, 3))
            leakage_wf.append(round(l_val, 3))
    
    elif fault_code == "E01":  # 交流窜入
        # 所有通道同步受到50Hz干扰
        ac_phase = random.uniform(0, 2 * math.pi)  # 随机初相位
        ac_amplitude = 45.0 + 15.0 * math.sin(current_time * 0.5)  # 干扰幅度
        
        for i in range(N_POINTS):
            t = current_time + (i / FS)
            
            # 50Hz交流干扰（所有通道同相位）
            ac_interference = ac_amplitude * math.sin(2 * math.pi * 50 * t + ac_phase)
            ac_harmonic_3 = ac_amplitude * 0.12 * math.sin(2 * math.pi * 150 * t + ac_phase)
            ac_harmonic_5 = ac_amplitude * 0.05 * math.sin(2 * math.pi * 250 * t + ac_phase)
            total_ac = ac_interference + ac_harmonic_3 + ac_harmonic_5
            
            # 1. 直流母线(+): 叠加交流干扰
            v_pos = V_BASE + total_ac + random.gauss(0, 1.5)
            
            # 2. 直流母线(-): 镜像+交流干扰（相位相同）
            v_neg = -V_BASE - total_ac + random.gauss(0, 1.5)
            
            # 3. 负载电流: 受交流影响，电流波动增大
            i_val = I_BASE + 0.8 * math.sin(2 * math.pi * 50 * t + ac_phase + 0.3) + \
                    random.gauss(0, 0.3)
            
            # 4. 漏电流: 轻微增加
            l_val = L_BASE * 2 + 0.01 * math.sin(2 * math.pi * 50 * t + ac_phase) + \
                    random.gauss(0, 0.01)
            
            voltage_pos.append(round(v_pos, 3))
            voltage_neg.append(round(v_neg, 3))
            current_wf.append(round(i_val, 3))
            leakage_wf.append(round(l_val, 3))
    
    elif fault_code == "E02":  # 绝缘故障
        for i in range(N_POINTS):
            t = current_time + (i / FS)
            
            # 1. 直流母线(+): 电压下降
            v_pos = V_BASE - 10.0 + 3.0 * math.sin(2 * math.pi * 50 * t) + \
                    random.gauss(0, 2.0)
            
            # 2. 直流母线(-): 镜像下降
            v_neg = -V_BASE + 10.0 - 3.0 * math.sin(2 * math.pi * 50 * t) + \
                    random.gauss(0, 2.0)
            
            # 3. 负载电流: 略有增加（漏电导致）
            i_val = I_BASE + 0.5 + 0.3 * math.sin(2 * math.pi * 50 * t) + \
                    random.gauss(0, 0.2)
            
            # 4. 漏电流: 显著增加（绝缘故障的主要特征）
            leakage_base = 35.0 + 15.0 * math.sin(current_time * 0.3)
            l_val = leakage_base + 8.0 * math.sin(2 * math.pi * 50 * t) + \
                    random.gauss(0, 3.0)
            # 间歇性脉冲（局部放电）
            if (i % 200) < 5:
                l_val += 10.0
            
            voltage_pos.append(round(v_pos, 3))
            voltage_neg.append(round(v_neg, 3))
            current_wf.append(round(i_val, 3))
            leakage_wf.append(round(max(0, l_val), 3))
    
    elif fault_code == "E03":  # 直流母线电容老化
        for i in range(N_POINTS):
            t = current_time + (i / FS)
            
            # 电容老化导致纹波增大（所有电压通道同步）
            ripple_100 = 12.0 * math.sin(2 * math.pi * 100 * t)
            ripple_200 = 6.0 * math.sin(2 * math.pi * 200 * t)
            ripple_300 = 3.0 * math.sin(2 * math.pi * 300 * t)
            total_ripple = ripple_100 + ripple_200 + ripple_300
            
            # 1. 直流母线(+): 大纹波
            v_pos = V_BASE + total_ripple + random.gauss(0, 1.5)
            
            # 2. 直流母线(-): 镜像大纹波
            v_neg = -V_BASE - total_ripple + random.gauss(0, 1.5)
            
            # 3. 负载电流: 纹波也增大
            i_val = I_BASE + 0.8 * math.sin(2 * math.pi * 100 * t) + \
                    0.4 * math.sin(2 * math.pi * 200 * t) + \
                    random.gauss(0, 0.3)
            
            # 4. 漏电流: 正常
            l_val = L_BASE + random.gauss(0, 0.01)
            
            voltage_pos.append(round(v_pos, 3))
            voltage_neg.append(round(v_neg, 3))
            current_wf.append(round(i_val, 3))
            leakage_wf.append(round(l_val, 3))
    
    elif fault_code == "E04":  # IGBT开路
        for i in range(N_POINTS):
            t = current_time + (i / FS)
            
            # 1&2. 直流电压: 不平衡，50Hz和100Hz波动
            imbalance = 8.0 * math.sin(2 * math.pi * 50 * t) + \
                       4.0 * math.sin(2 * math.pi * 100 * t)
            v_pos = V_BASE + imbalance + random.gauss(0, 1.5)
            v_neg = -V_BASE + imbalance * 0.8 + random.gauss(0, 1.5)  # 不对称加剧
            
            # 3. 负载电流: 半波缺失（关键特征）
            sine_wave = I_BASE + 3.0 * math.sin(2 * math.pi * 50 * t)
            if sine_wave < I_BASE:  # 负半周缺失
                sine_wave = I_BASE
            even_harmonic = 1.5 * math.sin(2 * math.pi * 100 * t)  # 偶次谐波
            i_val = sine_wave + even_harmonic + random.gauss(0, 0.2)
            
            # 4. 漏电流: 略有增加
            l_val = L_BASE * 1.5 + random.gauss(0, 0.01)
            
            voltage_pos.append(round(v_pos, 3))
            voltage_neg.append(round(v_neg, 3))
            current_wf.append(round(i_val, 3))
            leakage_wf.append(round(l_val, 3))
    
    elif fault_code == "E05":  # 接地故障
        for i in range(N_POINTS):
            t = current_time + (i / FS)
            
            # 1. 直流母线(+): 大幅下降（接地）
            ground_voltage = 15.0 + 5.0 * math.sin(current_time * 0.5)
            v_pos = ground_voltage + 2.0 * math.sin(2 * math.pi * 50 * t) + \
                    random.gauss(0, 2.0)
            
            # 2. 直流母线(-): 电压偏移到线电压负值
            offset_voltage = -720.0 + 20.0 * math.sin(current_time * 0.3)
            v_neg = offset_voltage + 5.0 * math.sin(2 * math.pi * 50 * t) + \
                    random.gauss(0, 3.0)
            
            # 3. 负载电流: 异常增大或不稳定
            i_val = I_BASE + 1.5 * math.sin(2 * math.pi * 50 * t) + \
                    random.gauss(0, 0.5)
            
            # 4. 漏电流: 显著增加（接地故障主要特征）
            leakage_base = 40.0 + 10.0 * math.sin(current_time * 0.4)
            l_val = leakage_base + 8.0 * math.sin(2 * math.pi * 50 * t) + \
                    random.gauss(0, 2.0)
            
            voltage_pos.append(round(v_pos, 3))
            voltage_neg.append(round(v_neg, 3))
            current_wf.append(round(i_val, 3))
            leakage_wf.append(round(max(0, l_val), 3))
    
    else:  # 未知故障：默认正常
        for i in range(N_POINTS):
            t = current_time + (i / FS)
            voltage_pos.append(round(V_BASE + random.gauss(0, 0.5), 3))
            voltage_neg.append(round(-V_BASE + random.gauss(0, 0.5), 3))
            current_wf.append(round(I_BASE + random.gauss(0, 0.1), 3))
            leakage_wf.append(round(L_BASE + random.gauss(0, 0.01), 3))
    
    return {
        'voltage_pos': voltage_pos,
        'voltage_neg': voltage_neg,
        'current': current_wf,
        'leakage': leakage_wf
    }

def generate_edge_data(channel, fault_code, time_offset, global_noise=GLOBAL_NOISE_LEVEL, correlated_data=None):
    """
    生成边缘计算节点的完整数据（STM32处理后的数据）
    使用工业标准：5.12kHz采样率，1024点，5Hz频率分辨率
    
    返回: (current_value, waveform_array, fft_spectrum_array)
    
    Args:
        channel: 通道配置字典
        fault_code: 故障代码 (E00-E05)
        time_offset: 时间偏移（用于连续波形生成）
        global_noise: 全局噪声级别
        correlated_data: 预先生成的关联波形数据（可选）
    
    Returns:
        tuple: (current_value, waveform, fft_spectrum)
            - current_value: 当前瞬时值（浮点数）
            - waveform: 波形数组（1024个浮点数，0.2秒快照）
            - fft_spectrum: FFT频谱数组（115个浮点数，0-575Hz，5Hz/bin）
    """
    channel_type = channel["type"]
    channel_label = channel.get("label", "")
    channel_id = channel.get("id", 0)
    
    # 如果提供了关联数据，直接使用
    if correlated_data is not None:
        if channel_id == 0:  # 直流母线(+)
            waveform = correlated_data['voltage_pos']
        elif channel_id == 1:  # 直流母线(-)
            waveform = correlated_data['voltage_neg']
        elif channel_id == 2:  # 负载电流
            waveform = correlated_data['current']
        elif channel_id == 3:  # 漏电流
            waveform = correlated_data['leakage']
        else:
            waveform = correlated_data['voltage_pos']  # 默认
        
        # 计算FFT频谱
        fft_spectrum = compute_partial_dft(waveform, num_bins=NUM_FFT_BINS)
        current_value = waveform[-1] if waveform else 0.0
        
        return current_value, waveform, fft_spectrum
    
    # 兼容旧版本：如果没有关联数据，使用独立生成（保持向后兼容）
    waveform = []
    
    waveform = []
    
    # 基础物理参数 - 明确处理负电压
    if channel_type == "DC":
        if "直流母线(-)" in channel_label or "DC-" in channel_label or "母线(-)" in channel_label:
            # 负电压：明确设置为-375V
            dc_bias = -375.0
        else:
            # 正电压：375V
            dc_bias = 375.0
    elif channel_type == "Current":
        dc_bias = 12.0
    elif channel_type == "Leakage":
        dc_bias = 0.02
    else:
        dc_bias = 0.0
    
    # 随机游走：让基础电压自然波动（符合实际工业场景）
    # 正常运行时，直流电压非常稳定，波动 < 0.5%
    if fault_code == "E00":
        dc_bias += random.gauss(0, 0.5)  # 正常状态：极小波动（±0.5V）
    else:
        dc_bias += random.gauss(0, 1.0)  # 故障状态：可能有更大波动
    
    # ========== 1. 波形生成（时域）==========
    # 关键：使用 time.time() + time_offset 让波形连续移动（动态效果）
    # 每个快照使用当前时间，确保波形看起来在"流动"
    current_time = time.time() + time_offset
    
    # E00: 正常状态（优化：更符合实际工业场景）
    if fault_code == "E00":
        for i in range(N_POINTS):
            t = i / FS  # 精确时间（秒），从0开始
            t_with_offset = t + current_time * 0.1  # 时间偏移让波形连续流动
            # 传感器噪声（高斯分布更真实）
            noise = random.gauss(0, global_noise * 0.5)
            
            if channel_type == "DC":
                # 正常直流母线：非常稳定，只有极小的纹波（<0.5V）
                # 实际直流系统纹波系数 < 0.1%，375V * 0.1% = 0.375V
                # 添加微小的开关噪声（10kHz以上，但采样率限制，表现为高频噪声）
                val = dc_bias + 0.2 * math.sin(2 * math.pi * 100 * t_with_offset) + \
                      0.1 * math.sin(2 * math.pi * 200 * t_with_offset) + \
                      noise
            elif channel_type == "Current":
                # 负载电流：有轻微的负载波动（±0.2A），叠加小纹波
                val = dc_bias + 0.2 * math.sin(2 * math.pi * 50 * t_with_offset) + \
                      0.1 * math.sin(2 * math.pi * 100 * t_with_offset) + \
                      noise
            elif channel_type == "Leakage":
                # 漏电流：正常值非常小且稳定（<0.05mA），只有传感器噪声
                val = dc_bias + noise * 0.1  # 噪声也减小
            else:
                val = dc_bias + noise
            waveform.append(round(val, 3))
    
    # E01: 交流窜入 - 50Hz强信号（必须出现在Bin 10）
    elif fault_code == "E01":
        if channel_type == "DC":
            for i in range(N_POINTS):
                t = current_time + (i / FS)  # 动态时间：连续移动，每次刷新波形都不同
                # 交流窜入：叠加大幅度的50Hz正弦波（30-80V幅度，模拟整流二极管击穿）
                # sin(2 * pi * 50 * t) - 50Hz正好对应Bin 10 (10 * 5Hz = 50Hz)
                ac_amplitude = 45.0 + 15.0 * math.sin(current_time * 0.5)  # 幅度在30-60V之间缓慢变化
                val = dc_bias + ac_amplitude * math.sin(2 * math.pi * 50 * t)
                # 添加150Hz三次谐波（Bin 30 = 150Hz），幅度约为基波的10-15%
                val += ac_amplitude * 0.12 * math.sin(2 * math.pi * 150 * t)
                # 添加250Hz五次谐波（Bin 50 = 250Hz），幅度更小
                val += ac_amplitude * 0.05 * math.sin(2 * math.pi * 250 * t)
                # 添加真实的高斯噪声（模拟传感器噪声）
                val += random.gauss(0, global_noise * 1.5)
                waveform.append(round(val, 3))
        elif channel_type == "Current":
            # 电流通道：可能受到交流窜入影响，有轻微50Hz波动
            for i in range(N_POINTS):
                t = current_time + (i / FS)
                val = dc_bias + 0.5 * math.sin(2 * math.pi * 50 * t) + \
                      0.2 * math.sin(2 * math.pi * 100 * t) + \
                      random.gauss(0, global_noise * 0.3)
                waveform.append(round(val, 3))
        else:
            # 其他通道正常（也使用动态时间）
            for i in range(N_POINTS):
                t = current_time + (i / FS)  # 动态时间，让波形连续移动
                val = dc_bias + 0.2 * math.sin(2 * math.pi * 100 * t) + \
                      random.gauss(0, global_noise * 0.5)
                waveform.append(round(val, 3))
    
    # E02: 绝缘故障 - 不规则脉冲（优化：更真实的绝缘故障特征）
    elif fault_code == "E02":
        if channel_type == "Leakage":
            for i in range(N_POINTS):
                t = current_time + (i / FS)  # 动态时间
                # 漏电流：绝缘故障时显著增加（20-60mA），有明显的50Hz成分
                # 模拟对地漏电流，通常与工频相关
                base_leakage = 35.0 + 15.0 * math.sin(current_time * 0.3)  # 基础漏电流在20-50mA之间波动
                # 50Hz交流成分（对地漏电流通常有工频特征）
                ac_component = 8.0 * math.sin(2 * math.pi * 50 * t)
                # 添加间歇性脉冲（模拟局部放电或间歇性接地）
                pulse = 10.0 if (i % 200) < 5 else 0.0  # 每200个点出现一次脉冲
                val = base_leakage + ac_component + pulse + random.gauss(0, 2.0)
                waveform.append(round(val, 3))
        elif channel_type == "DC":
            for i in range(N_POINTS):
                t = current_time + (i / FS)  # 动态时间
                # 直流电压：绝缘故障可能导致轻微下降（5-15V）和50Hz干扰
                voltage_drop = 10.0 + 5.0 * math.sin(current_time * 0.2)  # 电压下降在5-15V之间
                # 50Hz干扰（由于对地漏电流引起的电压波动）
                interference = 3.0 * math.sin(2 * math.pi * 50 * t)
                val = dc_bias - voltage_drop + interference + random.gauss(0, 1.0)
                waveform.append(round(val, 3))
        elif channel_type == "Current":
            # 电流可能略有增加（漏电流导致）
            for i in range(N_POINTS):
                t = current_time + (i / FS)
                val = dc_bias + 0.3 * math.sin(2 * math.pi * 50 * t) + \
                      random.gauss(0, global_noise * 0.3)
                waveform.append(round(val, 3))
        else:
            for i in range(N_POINTS):
                t = current_time + (i / FS)  # 动态时间
                val = dc_bias + 0.2 * math.sin(2 * math.pi * 100 * t) + \
                      random.gauss(0, global_noise * 0.5)
                waveform.append(round(val, 3))
    
    # E03: 电容老化 - 高频纹波（优化：更真实的电容老化特征）
    elif fault_code == "E03":
        if channel_type == "DC":
            for i in range(N_POINTS):
                t = current_time + (i / FS)  # 动态时间
                # 电容老化：滤波能力下降，纹波显著增加
                # 100Hz纹波（整流器二次谐波）- 主要成分
                ripple_100hz = 12.0 * math.sin(2 * math.pi * 100 * t)
                # 200Hz纹波（四次谐波）
                ripple_200hz = 6.0 * math.sin(2 * math.pi * 200 * t)
                # 300Hz纹波（六次谐波）
                ripple_300hz = 3.0 * math.sin(2 * math.pi * 300 * t)
                # 高频噪声（模拟ESR增加导致的高频噪声）
                # 由于采样率限制，高频成分会混叠，表现为随机噪声
                high_freq_noise = random.gauss(0, 2.0) if (i % 10) < 3 else 0.0  # 间歇性高频噪声
                val = dc_bias + ripple_100hz + ripple_200hz + ripple_300hz + \
                      high_freq_noise + random.gauss(0, 0.8)
                waveform.append(round(val, 3))
        elif channel_type == "Current":
            # 电流：纹波也会增加
            for i in range(N_POINTS):
                t = current_time + (i / FS)
                val = dc_bias + 0.8 * math.sin(2 * math.pi * 100 * t) + \
                      0.4 * math.sin(2 * math.pi * 200 * t) + \
                      random.gauss(0, global_noise * 0.3)
                waveform.append(round(val, 3))
        else:
            for i in range(N_POINTS):
                t = current_time + (i / FS)  # 动态时间
                val = dc_bias + 0.2 * math.sin(2 * math.pi * 100 * t) + \
                      random.gauss(0, global_noise * 0.5)
                waveform.append(round(val, 3))
    
    # E04: IGBT开路 - 半波缺失（优化：更真实的IGBT开路特征）
    elif fault_code == "E04":
        if channel_type == "Current":
            # IGBT开路：输出电流出现半波缺失（半波整流效果）
            # 正常应该是正弦波，但一个桥臂开路导致只有正半周或负半周
            for i in range(N_POINTS):
                t = current_time + (i / FS)  # 动态时间
                # 正常正弦波
                sine_wave = dc_bias + 3.0 * math.sin(2 * math.pi * 50 * t)
                # 半波整流：如果为负值，则置零（模拟缺失的开关相位）
                # 或者可以模拟正半周缺失
                if sine_wave < dc_bias:  # 负半周缺失
                    sine_wave = dc_bias
                # 添加偶次谐波（半波整流会产生偶次谐波）
                even_harmonic = 1.5 * math.sin(2 * math.pi * 100 * t)  # 100Hz偶次谐波
                val = sine_wave + even_harmonic + random.gauss(0, 0.2)
                waveform.append(round(val, 3))
        elif channel_type == "DC":
            # 直流电压：可能出现不平衡，有50Hz和100Hz成分
            for i in range(N_POINTS):
                t = current_time + (i / FS)
                # IGBT开路导致直流母线不平衡，出现50Hz和100Hz波动
                imbalance = 8.0 * math.sin(2 * math.pi * 50 * t) + \
                            4.0 * math.sin(2 * math.pi * 100 * t)
                val = dc_bias + imbalance + random.gauss(0, 1.0)
                waveform.append(round(val, 3))
        else:
            for i in range(N_POINTS):
                t = current_time + (i / FS)  # 动态时间
                val = dc_bias + 0.2 * math.sin(2 * math.pi * 100 * t) + \
                      random.gauss(0, global_noise * 0.5)
                waveform.append(round(val, 3))
    
    # E05: 接地故障 - 电压大幅偏移（优化：更真实的接地故障特征）
    elif fault_code == "E05":
        if channel_type == "DC":
            if "直流母线(+)" in channel_label or channel.get("range", [0, 800])[0] >= 0:
                # DC+接地：电压大幅下降，接近0V（金属性接地）
                # 实际可能不是完全0V，而是5-30V（取决于接地电阻）
                for i in range(N_POINTS):
                    t = current_time + (i / FS)
                    # 接地电压：在10-25V之间波动（取决于接地电阻和系统阻抗）
                    ground_voltage = 15.0 + 5.0 * math.sin(current_time * 0.5)  # 缓慢波动
                    # 可能有50Hz干扰（接地回路中的工频电流）
                    interference = 2.0 * math.sin(2 * math.pi * 50 * t)
                    val = ground_voltage + interference + random.gauss(0, 1.5)
                    waveform.append(round(val, 3))
            elif "直流母线(-)" in channel_label or channel.get("range", [-800, 0])[0] < 0:
                # DC-接地：电压大幅偏移到接近线电压的负值
                # 如果DC+接地，DC-会偏移到接近-750V（750V = 375V * 2）
                for i in range(N_POINTS):
                    t = current_time + (i / FS)
                    # 偏移电压：接近线电压的负值
                    offset_voltage = -720.0 + 20.0 * math.sin(current_time * 0.3)  # 在-700V到-740V之间
                    # 50Hz干扰
                    interference = 5.0 * math.sin(2 * math.pi * 50 * t)
                    val = offset_voltage + interference + random.gauss(0, 3.0)
                    waveform.append(round(val, 3))
            else:
                for i in range(N_POINTS):
                    t = current_time + (i / FS)  # 动态时间
                    val = dc_bias + 0.2 * math.sin(2 * math.pi * 100 * t) + \
                          random.gauss(0, global_noise * 0.5)
                    waveform.append(round(val, 3))
        elif channel_type == "Leakage":
            # 接地故障时漏电流显著增加
            for i in range(N_POINTS):
                t = current_time + (i / FS)
                # 接地故障导致大量漏电流（30-60mA）
                base_leakage = 40.0 + 10.0 * math.sin(current_time * 0.4)
                # 50Hz成分（接地回路中的工频电流）
                ac_component = 8.0 * math.sin(2 * math.pi * 50 * t)
                val = base_leakage + ac_component + random.gauss(0, 2.0)
                waveform.append(round(val, 3))
        elif channel_type == "Current":
            # 电流可能异常（接地导致电流路径改变）
            for i in range(N_POINTS):
                t = current_time + (i / FS)
                val = dc_bias + 1.5 * math.sin(2 * math.pi * 50 * t) + \
                      random.gauss(0, global_noise * 0.5)
                waveform.append(round(val, 3))
        else:
            for i in range(N_POINTS):
                t = current_time + (i / FS)  # 动态时间
                val = dc_bias + 0.2 * math.sin(2 * math.pi * 100 * t) + \
                      random.gauss(0, global_noise * 0.5)
                waveform.append(round(val, 3))
    
    # 未知故障代码：默认正常
    else:
        for i in range(N_POINTS):
            t = current_time + (i / FS)  # 动态时间
            val = dc_bias + 0.5 * math.sin(2 * math.pi * 300 * t) + random.uniform(-0.1, 0.1)
            waveform.append(round(val, 3))
    
    # ========== 2. 频谱生成（频域）- 使用真实DFT计算 ==========
    # 计算前115个bin（0-575Hz），覆盖550Hz范围，Bin 10 = 50Hz
    fft_spectrum = compute_partial_dft(waveform, num_bins=NUM_FFT_BINS)
    
    # 当前值 = 波形最后一个值
    current_value = waveform[-1] if waveform else dc_bias
    
    return current_value, waveform, fft_spectrum

def test_server_connection():
    """测试服务器连接是否可用"""
    global SERVER_URL
    try:
        print(f"[连接测试] 🔍 正在测试服务器连接: {SERVER_URL}")
        response = requests.get(f"{SERVER_URL}/api/get_active_nodes", timeout=3)
        if response.status_code == 200:
            print(f"[连接测试] ✅ 服务器连接成功: {SERVER_URL}")
            return True
        elif response.status_code == 401:
            # 登录态不足也说明服务器可达，避免误判导致自动切换端口
            print(f"[连接测试] ✅ 服务器可达（需要登录鉴权，HTTP 401）: {SERVER_URL}")
            return True
        else:
            print(f"[连接测试] ⚠️  服务器响应异常: {response.status_code}")
            return False
    except requests.exceptions.ConnectionError:
        print(f"[连接测试] ❌ 无法连接到服务器: {SERVER_URL}")
        print(f"           请确保服务器正在运行，并且地址正确")

        # 如果用户没有显式指定服务器地址，则自动尝试本机 5000/5002
        if not _SERVER_URL_EXPLICIT:
            for alt_url in _try_local_fallback_urls():
                try:
                    r = requests.get(f"{alt_url}/api/get_active_nodes", timeout=2)
                    if r.status_code == 200:
                        # 自动切换到可用地址
                        SERVER_URL = alt_url
                        _rebuild_endpoints()
                        print(f"           已自动切换服务器地址为: {SERVER_URL}")
                        return True
                except Exception:
                    pass
        return False
    except requests.exceptions.Timeout:
        print(f"[连接测试] ⏱️  服务器连接超时: {SERVER_URL}")
        return False
    except Exception as e:
        print(f"[连接测试] ❌ 连接测试失败: {e}")
        return False

def background_heartbeat_loop():
    """后台心跳循环 - 在独立线程中运行，定期发送心跳到Web界面"""
    print("[心跳服务] 🟢 心跳发现服务已启动...")
    print(f"[心跳服务] 目标端点: {HEARTBEAT_ENDPOINT}")
    print(f"[心跳服务] ⚡ 更新频率: 50Hz (每20ms发送一次心跳)")
    
    # 启动时测试服务器连接
    if not test_server_connection():
        print("[心跳服务] ⚠️  服务器连接测试失败，心跳服务将继续尝试连接...")
    
    time_offset = 0
    error_count = {}  # 记录每个节点的错误计数，用于限制日志输出频率
    last_error_time = {}  # 记录每个节点最后一次错误的时间
    
    while True:
        try:
            # 使用锁保护 NODES_CONFIG 的访问
            with CONFIG_LOCK:
                # 创建副本以避免在迭代时修改列表
                nodes_to_process = list(NODES_CONFIG)
            
            # 遍历所有节点配置，发送心跳（快速发送，不阻塞）
            for node_config in nodes_to_process:
                try:
                    report_mode = (node_config.get("report_mode") or "summary").lower()
                    # 为每个节点使用独立的时间偏移，让波形变化更明显
                    node_time_offset = time_offset + hash(node_config["node_id"]) % 100 * 0.001
                    
                    # 生成通道完整数据（包含波形和FFT）
                    channels_with_data = []
                    fault_code = node_config.get("fault_code", "E00")
                    
                    # ========== 新版本：使用关联波形生成 ==========
                    # 一次性生成4个通道的关联波形
                    correlated_waveforms = generate_correlated_waveforms(
                        fault_code, node_time_offset, GLOBAL_NOISE_LEVEL
                    )
                    
                    for channel in node_config["channels"]:
                        channel_copy = channel.copy()
                        
                        # 使用关联波形数据生成边缘计算数据
                        current_value, waveform, fft_spectrum = generate_edge_data(
                            channel, fault_code, node_time_offset, GLOBAL_NOISE_LEVEL,
                            correlated_data=correlated_waveforms  # 传入关联数据
                        )
                        
                        # 将数据添加到通道配置中（确保所有必需字段都存在）
                        channel_copy["value"] = current_value
                        if report_mode == "full":
                            channel_copy["waveform"] = waveform if isinstance(waveform, list) else []  # 1024个采样点
                            channel_copy["fft_spectrum"] = fft_spectrum if isinstance(fft_spectrum, list) else []  # FFT频谱
                        else:
                            channel_copy["waveform"] = []
                            channel_copy["fft_spectrum"] = []
                        
                        # 确保必需字段存在
                        if "label" not in channel_copy:
                            channel_copy["label"] = f"通道{channel_copy.get('id', 0)}"
                        if "id" not in channel_copy:
                            channel_copy["id"] = channel.get("id", 0)
                        
                        channels_with_data.append(channel_copy)
                    
                    # 构建心跳负载
                    payload = {
                        "node_id": node_config["node_id"],
                        "status": node_config["status"],
                        "fault_code": node_config.get("fault_code", "E00"),  # 包含故障代码
                        "location": node_config.get("location", "N/A"),  # 包含位置信息
                        "channels": channels_with_data,  # 包含完整数据（value, waveform, fft_spectrum）
                        "timestamp": time.time()
                    }
                    
                    # 发送心跳（增加超时时间，确保局域网连接稳定）
                    try:
                        response = requests.post(
                            HEARTBEAT_ENDPOINT,
                            headers=_get_device_headers(),
                            json=payload,
                            timeout=5.0  # 增加到5秒超时，确保局域网连接稳定
                        )
                    except requests.exceptions.Timeout:
                        # 超时错误
                        node_id = node_config['node_id']
                        current_time = time.time()
                        if node_id not in last_error_time or current_time - last_error_time[node_id] > 5:
                            print(f"[心跳服务] ⏱️  {node_id} 心跳超时（服务器无响应，请检查网络连接）")
                            last_error_time[node_id] = current_time
                        continue
                    except requests.exceptions.ConnectionError as e:
                        # 连接错误（服务器不可达）
                        node_id = node_config['node_id']
                        current_time = time.time()
                        if node_id not in last_error_time or current_time - last_error_time[node_id] > 5:
                            print(f"[心跳服务] 🔌 {node_id} 连接失败: 无法连接到服务器 {SERVER_URL}")
                            print(f"           请检查:")
                            print(f"           1. 服务器是否正在运行")
                            print(f"           2. 服务器地址是否正确: {SERVER_URL}")
                            print(f"           3. 防火墙是否允许连接")
                            last_error_time[node_id] = current_time
                        continue
                    except requests.exceptions.RequestException as e:
                        # 其他网络错误
                        node_id = node_config['node_id']
                        current_time = time.time()
                        if node_id not in last_error_time or current_time - last_error_time[node_id] > 5:
                            print(f"[心跳服务] ❌ {node_id} 网络错误: {str(e)}")
                            last_error_time[node_id] = current_time
                        continue
                    except Exception as e:
                        # 其他未知错误
                        node_id = node_config['node_id']
                        current_time = time.time()
                        if node_id not in last_error_time or current_time - last_error_time[node_id] > 5:
                            print(f"[心跳服务] ❌ {node_id} 心跳发送异常: {type(e).__name__}: {str(e)}")
                            last_error_time[node_id] = current_time
                        continue
                    
                    # 检查响应状态
                    if response.status_code == 200:
                        # 检查服务器返回的命令（远程故障清除）
                        try:
                            resp_data = response.json()
                            if resp_data.get('command') == 'reset':
                                node_id = node_config['node_id']
                                print(f"\n[远程命令] 🎯 服务器请求重置节点: {node_id}")
                                
                                # 执行重置逻辑（与 'clear' 命令相同）
                                config_index = None
                                for idx, nc in enumerate(NODES_CONFIG):
                                    if nc['node_id'] == node_id:
                                        config_index = idx
                                        break
                                
                                if config_index is not None:
                                    # 1. 重置 NODES_CONFIG 状态
                                    NODES_CONFIG[config_index]['status'] = 'online'
                                    NODES_CONFIG[config_index]['fault_code'] = 'E00'
                                    
                                    # 2. 重置节点实例状态（如果存在）
                                    node_index = None
                                    for idx, node in NODES.items():
                                        if node.device_id == node_id:
                                            node_index = idx
                                            break
                                    
                                    if node_index is not None:
                                        with NODES_LOCK:
                                            NODES[node_index].fault_code = 'E00'
                                    
                                    print(f"[远程命令] ✅ 节点 {node_id} 已重置为在线状态（物理状态已恢复）")
                                    print(f"       心跳状态已同步: online (故障代码: E00)")

                            report_mode = resp_data.get('report_mode')
                            if report_mode:
                                report_mode = str(report_mode).lower()
                                if report_mode in ('summary', 'full'):
                                    with CONFIG_LOCK:
                                        for idx, nc in enumerate(NODES_CONFIG):
                                            if nc['node_id'] == node_config['node_id']:
                                                NODES_CONFIG[idx]['report_mode'] = report_mode
                                                break
                        except (ValueError, KeyError) as e:
                            # JSON解析错误或缺少字段，忽略
                            pass
                    else:
                        # 显示详细错误信息（包括服务器返回的错误消息）
                        node_id = node_config['node_id']
                        current_time = time.time()
                        
                        # 限制错误日志频率（每5秒最多输出一次）
                        if node_id not in last_error_time or current_time - last_error_time[node_id] > 5:
                            try:
                                error_msg = response.json().get('error', '未知错误')
                                print(f"[心跳服务] ⚠️  {node_id} 心跳失败: {response.status_code} - {error_msg}")
                                if response.status_code == 500:
                                    print(f"           服务器内部错误，请检查服务器日志")
                            except:
                                error_text = response.text[:200] if hasattr(response, 'text') else '无响应内容'
                                print(f"[心跳服务] ⚠️  {node_id} 心跳失败: {response.status_code}")
                                print(f"           响应内容: {error_text}")
                            
                            last_error_time[node_id] = current_time
                            error_count[node_id] = error_count.get(node_id, 0) + 1
                        else:
                            error_count[node_id] = error_count.get(node_id, 0) + 1
                
                except Exception as e:
                    # 捕获所有其他异常（包括数据处理错误等）
                    node_id = node_config.get('node_id', 'unknown')
                    current_time = time.time()
                    if node_id not in last_error_time or current_time - last_error_time[node_id] > 5:
                        print(f"[心跳服务] ❌ {node_id} 处理异常: {type(e).__name__}: {str(e)}")
                        last_error_time[node_id] = current_time
            
            time_offset += 0.02  # 增加时间偏移步长，让波形变化更明显
            time.sleep(0.02)  # 每0.02秒发送一次心跳（20ms，50Hz更新频率，高频刷新）
            
        except Exception as e:
            print(f"[心跳服务] ❌ 网络错误: {e}")
            time.sleep(2)  # 出错时等待2秒再重试

def generate_physics_based_value(channel, fault_code, time_offset=0):
    """根据故障代码生成物理上正确的通道值"""
    channel_type = channel["type"]
    channel_label = channel.get("label", "")
    range_min, range_max = channel["range"]
    variation = channel.get("variation", 1)
    
    # E00: 正常状态（优化：更符合实际工业标准）
    if fault_code == "E00":
        if channel_type == "DC":
            # 直流电压：正常值在375V左右，非常稳定（纹波系数 < 0.1%）
            base_value = 375.0 if range_min >= 0 else -375.0
            # 正常纹波 < 0.5V，使用高斯分布更真实
            value = base_value + random.gauss(0, 0.3)
        elif channel_type == "Current":
            # 负载电流：正常值在12A左右，有轻微波动（±0.2A）
            base_value = 12.0
            value = base_value + random.gauss(0, 0.15)
        elif channel_type == "Leakage":
            # 漏电流：正常值非常小，约0.02-0.05mA（符合工业标准）
            base_value = 0.03
            value = base_value + random.gauss(0, 0.01)
        else:
            base_value = (range_min + range_max) / 2
            value = base_value + random.gauss(0, variation * 0.5)
    
    # E01: 交流窜入（优化：更真实的交流窜入特征）
    elif fault_code == "E01":
        if channel_type == "DC":
            # 直流电压：叠加大幅度的50Hz交流信号（30-70V纹波）
            base_value = 375.0 if range_min >= 0 else -375.0
            ac_amplitude = 40.0 + 20.0 * math.sin(time_offset * 0.5)  # 30-60V纹波，缓慢变化
            ac_component = ac_amplitude * math.sin(2 * math.pi * 50 * time_offset)
            # 150Hz三次谐波（约为基波的10-15%）
            harmonic_150hz = ac_amplitude * 0.12 * math.sin(2 * math.pi * 150 * time_offset)
            value = base_value + ac_component + harmonic_150hz + random.gauss(0, 1.5)
        elif channel_type == "Current":
            # 电流：可能受到交流窜入影响，有轻微50Hz波动
            base_value = 12.0
            value = base_value + 0.3 * math.sin(2 * math.pi * 50 * time_offset) + \
                    random.gauss(0, 0.2)
        elif channel_type == "Leakage":
            # 漏电流：交流窜入可能导致轻微增加
            base_value = 0.08
            value = base_value + random.gauss(0, 0.02)
        else:
            base_value = (range_min + range_max) / 2
            value = base_value + random.gauss(0, variation * 0.5)
    
    # E02: 绝缘故障（优化：更真实的绝缘故障特征）
    elif fault_code == "E02":
        if "绝缘" in channel_label or channel_type == "ISO":
            # 绝缘电阻：下降到0.05-0.15 MΩ（严重绝缘故障）
            value = 0.1 + random.gauss(0, 0.03)
        elif channel_type == "Leakage":
            # 漏电流：飙升到30-70mA（绝缘故障导致对地漏电流增加）
            base_leakage = 45.0 + 15.0 * math.sin(time_offset * 0.3)
            value = base_leakage + random.gauss(0, 5.0)
        elif channel_type == "DC":
            # 直流电压：可能下降5-20V（取决于绝缘故障严重程度）
            base_value = 375.0 if range_min >= 0 else -375.0
            voltage_drop = 12.0 + 5.0 * math.sin(time_offset * 0.2)
            value = base_value - voltage_drop + random.gauss(0, 2.0)
        else:
            base_value = (range_min + range_max) / 2
            value = base_value + random.gauss(0, variation * 0.5)
    
    # E03: 电容老化（优化：更真实的电容老化特征）
    elif fault_code == "E03":
        if channel_type == "DC":
            # 直流电压：纹波显著增加（8-15V），多频率成分
            base_value = 375.0 if range_min >= 0 else -375.0
            # 100Hz纹波（主要成分）
            ripple_100hz = (8.0 + 4.0 * math.sin(time_offset * 0.3)) * math.sin(2 * math.pi * 100 * time_offset)
            # 200Hz纹波
            ripple_200hz = 5.0 * math.sin(2 * math.pi * 200 * time_offset)
            # 300Hz纹波
            ripple_300hz = 2.5 * math.sin(2 * math.pi * 300 * time_offset)
            value = base_value + ripple_100hz + ripple_200hz + ripple_300hz + \
                    random.gauss(0, 1.2)
        elif channel_type == "Current":
            # 电流：纹波也会增加
            base_value = 12.0
            ripple = 0.6 * math.sin(2 * math.pi * 100 * time_offset)
            value = base_value + ripple + random.gauss(0, 0.2)
        else:
            base_value = (range_min + range_max) / 2
            value = base_value + random.gauss(0, variation * 0.5)
    
    # E04: IGBT开路（优化：更真实的IGBT开路特征）
    elif fault_code == "E04":
        if channel_type == "Current":
            # 电流：半波缺失，平均值约为正常值的一半
            # 正常12A，半波缺失后约为6A（但波形是半波整流）
            base_current = 6.0 + 3.0 * max(0, math.sin(2 * math.pi * 50 * time_offset))
            value = base_current + random.gauss(0, 0.3)
        elif channel_type == "DC":
            # 直流电压：可能出现不平衡，有50Hz和100Hz波动
            base_value = 375.0 if range_min >= 0 else -375.0
            imbalance = 6.0 * math.sin(2 * math.pi * 50 * time_offset) + \
                        3.0 * math.sin(2 * math.pi * 100 * time_offset)
            value = base_value + imbalance + random.gauss(0, 2.0)
        else:
            base_value = (range_min + range_max) / 2
            value = base_value + random.gauss(0, variation * 0.5)
    
    # E05: 接地故障（优化：更真实的接地故障特征）
    elif fault_code == "E05":
        if channel_type == "DC":
            if "直流母线(+)" in channel_label or range_min >= 0:
                # DC+接地：电压大幅下降，接近0V（10-30V，取决于接地电阻）
                ground_voltage = 18.0 + 8.0 * math.sin(time_offset * 0.4)
                value = ground_voltage + random.gauss(0, 2.0)
            elif "直流母线(-)" in channel_label or range_min < 0:
                # DC-接地：电压大幅偏移到接近线电压的负值（-700V到-750V）
                offset_voltage = -725.0 + 15.0 * math.sin(time_offset * 0.3)
                value = offset_voltage + random.gauss(0, 5.0)
            else:
                base_value = 375.0 if range_min >= 0 else -375.0
                value = base_value + random.gauss(0, variation * 0.5)
        elif channel_type == "Leakage":
            # 漏电流：接地故障导致大量漏电流（40-70mA）
            base_leakage = 50.0 + 12.0 * math.sin(time_offset * 0.4)
            value = base_leakage + random.gauss(0, 4.0)
        else:
            base_value = (range_min + range_max) / 2
            value = base_value + random.gauss(0, variation * 0.5)
    
    # 未知故障代码：使用默认逻辑
    else:
        if channel_type == "DC":
            base_value = 375.0 if range_min >= 0 else -375.0
            value = base_value + random.uniform(-variation, variation)
        elif channel_type == "Current":
            base_value = 12.0
            value = base_value + random.uniform(-variation, variation)
        elif channel_type == "Leakage":
            base_value = 0.02
            value = base_value + random.uniform(-variation, variation)
        else:
            base_value = (range_min + range_max) / 2
            value = base_value + random.uniform(-variation, variation)
    
    # 限制在范围内
    return round(max(range_min, min(range_max, value)), 3)

def main():
    """主函数：启动心跳服务和控制台命令线程（动态节点管理）"""
    # 启动前优先应用命令行参数（例如：--server http://localhost:5002）
    apply_server_config_from_cli()

    print("=" * 70)
    print("模拟STM32H7硬件节点 - 动态节点管理器 + 心跳发现系统")
    print("=" * 70)
    print("功能:")
    print("  1. 心跳发现服务：后台线程定期发送心跳到Web界面")
    print("  2. 动态节点管理：通过控制台命令动态注册/注销节点")
    print("  3. 控制台命令：通过命令手动触发故障和控制在线/离线状态")
    print("=" * 70)
    print()
    print("💡 提示: 使用 'add <device_id> <location>' 命令注册节点")
    print("   示例: add STM32_Node_001 风机#1直流母线")
    print("   或: add 1 风机#1直流母线  (自动生成设备ID)")
    print()
    
    # 启动心跳服务线程（后台守护线程）
    heartbeat_thread = threading.Thread(
        target=background_heartbeat_loop,
        daemon=True,  # 守护线程，主程序退出时自动结束
        name="Heartbeat-Service"
    )
    heartbeat_thread.start()
    print("✅ 心跳发现服务已启动（后台线程）")
    time.sleep(0.5)  # 短暂延迟，让心跳服务初始化
    
    # 启动控制台命令线程
    console_thread = threading.Thread(
        target=console_menu,
        daemon=True,  # 设置为守护线程，主程序退出时自动结束
        name="Console-Menu"
    )
    console_thread.start()
    
    print("所有服务正在运行中...")
    print("💡 使用 'add <device_id> <location>' 命令开始注册节点\n")
    
    try:
        # 主线程保持运行，等待用户命令
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\n\n⚠️  收到停止信号，正在关闭所有节点...")
        # 停止所有节点
        with NODES_LOCK:
            for node in NODES.values():
                node.running = False
        print("✅ 所有节点已停止")
        sys.exit(0)

if __name__ == "__main__":
    main()
