"""
故障诊断知识图谱模块
定义故障代码、知识图谱数据和相关处理函数
"""
import json
import hashlib
from datetime import datetime, timedelta
from statistics import mean, pstdev
from typing import Any

REASONING_GRAPH_SCHEMA_VERSION = "edgewind.reasoning_graph.v1"
GRAPH_CATEGORIES = [
    {"name": "故障", "itemStyle": {"color": "#ef4444"}},
    {"name": "根本原因", "itemStyle": {"color": "#f59e0b"}},
    {"name": "解决方案", "itemStyle": {"color": "#10b981"}},
    {"name": "证据", "itemStyle": {"color": "#06b6d4"}},
    {"name": "人工复核", "itemStyle": {"color": "#8b5cf6"}},
    {"name": "数据不足", "itemStyle": {"color": "#64748b"}},
    {"name": "风险", "itemStyle": {"color": "#dc2626"}},
]

# ==================== 故障代码映射 ====================
# E00=Normal, E01=AC Intrusion, E02=Insulation Fault, E03=DC Capacitor Aging,
# E04=IGBT Open Circuit, E05=DC Bus Grounding, E06=PWM Abnormality

FAULT_CODE_MAP = {
    "E00": None,  # Normal - 无故障
    "E01": "AC_INTRUSION",
    "E02": "INSULATION_FAULT",
    "E03": "DC_CAPACITOR_AGING",
    "E04": "IGBT_OPEN_CIRCUIT",
    "E05": "DC_BUS_GROUNDING",
    "E06": "PWM_ABNORMAL"
}

FAULT_REASONING_KB = {
    "E01": {
        "typical_evidence": [
            "直流母线出现 50Hz/150Hz 低频交流分量",
            "正负母线电压同步纹波增大",
            "整流桥或滤波回路温升、噪声或保护动作异常",
        ],
        "diagnostic_rules": [
            {"if": "FFT 工频及三次谐波同时升高", "then": "优先怀疑整流侧隔离或滤波链路失效"},
            {"if": "母线电压均值正常但标准差显著增大", "then": "优先排查交流窜入而非稳态过压"},
            {"if": "交流输入侧操作后突发", "then": "检查浪涌导致的整流桥臂击穿"},
        ],
        "verification_steps": [
            "断电隔离后测量整流桥六个臂的正反向压降",
            "检查直流母线滤波电容鼓包、漏液和温升",
            "复核交流侧 SPD 和接地连续性",
        ],
    },
    "E02": {
        "typical_evidence": [
            "漏电流通道持续偏高或突增",
            "正/负母线对地电压不平衡或剧烈波动",
            "快照中某一母线或负载通道波形标准差明显增大",
        ],
        "diagnostic_rules": [
            {"if": "漏电流高且母线对地电压波动", "then": "优先怀疑绝缘下降、湿气侵入或间歇性接地"},
            {"if": "仅漏电流异常但负载电流稳定", "then": "优先排查对地泄漏路径而非负载侧故障"},
            {"if": "历史窗口缺失或无趋势", "then": "必须把结论标记为快照级推断并要求人工复核"},
        ],
        "verification_steps": [
            "使用绝缘测试仪测量正/负母线对地绝缘电阻",
            "检查电缆密封、接线端子、柜内凝露和污秽",
            "分段断开支路，定位泄漏回路或间歇性接地点",
        ],
    },
    "E03": {
        "typical_evidence": [
            "母线电压纹波长期缓慢增大",
            "负载阶跃时电压恢复变慢",
            "高频纹波和温升同时升高",
        ],
        "diagnostic_rules": [
            {"if": "纹波随时间缓慢恶化", "then": "优先怀疑电容容量衰减或 ESR 增大"},
            {"if": "温度升高后异常更明显", "then": "检查电容老化和散热条件"},
            {"if": "突发大幅异常", "then": "不要直接归因老化，先排除接线松动和短路冲击"},
        ],
        "verification_steps": [
            "停机后测量电容容量和 ESR",
            "检查电容鼓包、漏液、固定件松动和母排连接",
            "复核柜内温度、风道和散热器状态",
        ],
    },
    "E04": {
        "typical_evidence": [
            "某相电流缺失或显著低于其他相",
            "驱动脉冲存在但功率输出不响应",
            "开关频率相关频谱分量异常",
        ],
        "diagnostic_rules": [
            {"if": "驱动正常但输出缺相", "then": "优先怀疑 IGBT 开路或功率回路断开"},
            {"if": "故障伴随过流保护", "then": "先排除短路和驱动误触发"},
            {"if": "故障间歇出现", "then": "重点检查焊点、母排和端子松动"},
        ],
        "verification_steps": [
            "检查 IGBT 驱动电源、门极波形和故障反馈",
            "测量功率模块端子压降和回路连续性",
            "复核散热器、压接力矩和端子连接",
        ],
    },
    "E05": {
        "typical_evidence": [
            "母线对地电压偏移且漏电流升高",
            "接地电阻或 PE 连续性异常",
            "故障与湿度、柜门开启或电缆移动相关",
        ],
        "diagnostic_rules": [
            {"if": "漏电流与对地电压偏移同时出现", "then": "优先怀疑直流母线接地故障"},
            {"if": "接地线松动后出现波动", "then": "先恢复保护接地再复测"},
            {"if": "只出现单次尖峰", "then": "标记为暂态风险并要求继续观察"},
        ],
        "verification_steps": [
            "测量 PE 接地电阻和柜体等电位连接",
            "分支路绝缘测试，定位接地点",
            "检查电缆护套破损、进水和端子松动",
        ],
    },
    "E06": {
        "typical_evidence": [
            "PWM 频率或占空比异常跳变",
            "控制输出与采样反馈不一致",
            "频谱中开关频率侧带异常",
        ],
        "diagnostic_rules": [
            {"if": "PWM 命令正常但反馈异常", "then": "优先检查驱动链路和功率执行端"},
            {"if": "PWM 命令本身异常", "then": "优先检查控制参数、同步时钟和软件状态"},
            {"if": "异常与通信中断同步", "then": "排查控制链路和 EMI 干扰"},
        ],
        "verification_steps": [
            "用示波器复核 PWM 频率、占空比和死区",
            "检查驱动板供电、隔离器和门极电阻",
            "复核控制参数、同步信号和通信错误计数",
        ],
    },
}

# ==================== 故障诊断知识图谱 ====================

EXPERT_KNOWLEDGE_LIBRARY = {
    "E01": {
        "extra_causes": [
            {"name": "整流桥二极管反向漏电", "description": "桥臂未完全击穿但反向漏电升高，直流母线叠加工频纹波。", "keywords": ["50hz", "150hz", "voltage_pos", "voltage_neg", "纹波", "直流母线"]},
            {"name": "输入侧EMI滤波失效", "description": "输入滤波电容或共模电感失效，交流分量被耦合到直流侧。", "keywords": ["emi", "滤波", "交流", "std", "ripple"]},
            {"name": "直流链路薄膜电容短漏", "description": "DC-Link 薄膜电容绝缘下降或短漏，造成低频纹波和温升。", "keywords": ["电容", "dc-link", "ripple", "temperature"]},
            {"name": "接触器触点粘连", "description": "预充或旁路接触器粘连，使异常交流路径持续存在。", "keywords": ["接触器", "突变", "history_delta"]},
            {"name": "浪涌保护器泄漏", "description": "SPD 老化后对地泄漏增大，形成交流窜入或共模噪声。", "keywords": ["spd", "leakage", "漏电流", "ground"]},
            {"name": "母线采样参考漂移", "description": "采样地或参考源漂移导致直流母线纹波被放大呈现。", "keywords": ["采样", "reference", "voltage_pos", "voltage_neg"]},
        ],
        "evidence_patterns": [
            {"name": "正母线大幅纹波", "description": "直流母线(+)标准差或峰峰值显著增大。", "keywords": ["直流母线(+)", "voltage_pos", "std", "range"], "related_causes": [1, "extra_1", "extra_3"]},
            {"name": "负母线同步纹波", "description": "直流母线(-)与正母线同步出现低频波动。", "keywords": ["直流母线(-)", "voltage_neg", "std"], "related_causes": [1, 2]},
            {"name": "工频/三次谐波峰值", "description": "FFT Top-K 出现 50Hz/150Hz 附近峰值。", "keywords": ["fft", "50", "150", "top_bin"], "related_causes": [1, "extra_1"]},
            {"name": "漏电流同时抬升", "description": "漏电流通道与母线纹波同时异常。", "keywords": ["漏电流", "leakage"], "related_causes": [3, "extra_5"]},
            {"name": "负载电流未同步突变", "description": "负载电流稳定而母线电压波动，指向电源侧窜入。", "keywords": ["负载电流", "current"], "related_causes": ["extra_2"]},
            {"name": "历史窗口纹波持续", "description": "一小时历史窗口内母线波动没有快速恢复。", "keywords": ["history", "delta", "voltage"], "related_causes": ["extra_3", "extra_4"]},
            {"name": "上电/切换后突发", "description": "故障紧随接触器或输入侧操作出现。", "keywords": ["突发", "after", "history_delta"], "related_causes": ["extra_4"]},
            {"name": "共模噪声增强", "description": "正负母线对地波动和漏电流同向增强。", "keywords": ["common", "ground", "漏电流"], "related_causes": [3, "extra_5"]},
        ],
        "checks": ["整流桥六臂二极管档复测", "输入EMI滤波器绝缘和容量复测", "DC-Link 电容温升和漏液检查", "接触器触点电阻和粘连检查", "SPD 脱扣/泄漏电流检查", "母线采样参考地连续性复核"],
        "actions": ["隔离交流输入后复测母线纹波", "更换异常整流桥或滤波组件", "更换鼓包/漏液 DC-Link 电容", "处理接触器粘连并做动作寿命测试", "更换泄漏 SPD 并复测接地", "保留 10 分钟观察窗口确认纹波消失"],
        "risks": ["电容过热爆裂", "保护误动作导致非计划停机", "交流侧异常继续扩大到逆变器输入"],
    },
    "E02": {
        "extra_causes": [
            {"name": "电缆护套破损", "description": "外力磨损或压伤使导体对柜体/桥架泄漏。", "keywords": ["漏电流", "leakage", "电缆", "range"]},
            {"name": "端子排受潮凝露", "description": "端子表面形成水膜或污秽导电路径。", "keywords": ["湿气", "凝露", "漏电流", "after"]},
            {"name": "绝缘监测回路漂移", "description": "IMD 或采样回路偏移，造成绝缘故障误判或放大。", "keywords": ["采样", "reference", "history"]},
            {"name": "负载支路单点接地", "description": "某一路负载电缆或设备形成单点对地泄漏。", "keywords": ["负载电流", "current", "leakage"]},
            {"name": "母排绝缘套管压伤", "description": "母排绝缘层被固定件压伤后对柜体放电。", "keywords": ["直流母线", "voltage_pos", "voltage_neg", "range"]},
            {"name": "柜内粉尘盐雾污染", "description": "粉尘、盐雾或碳化痕迹降低爬电距离。", "keywords": ["污染", "std", "leakage"]},
        ],
        "evidence_patterns": [
            {"name": "漏电流通道高位", "description": "漏电流当前值或最大值持续偏高。", "keywords": ["漏电流", "leakage", "current_value"], "related_causes": [1, "extra_1", "extra_4"]},
            {"name": "母线对地不平衡", "description": "正负母线统计差异扩大，提示绝缘支路不平衡。", "keywords": ["直流母线(+)", "直流母线(-)", "voltage"], "related_causes": [1, "extra_5"]},
            {"name": "湿度相关间歇波动", "description": "故障呈间歇性，恢复前后快照差异明显。", "keywords": ["before_recovery", "after_recovery", "湿气"], "related_causes": [2, "extra_2"]},
            {"name": "负载电流平稳但泄漏异常", "description": "负载侧平均值稳定，泄漏通道异常更突出。", "keywords": ["负载电流", "漏电流"], "related_causes": ["extra_4"]},
            {"name": "单通道标准差增大", "description": "某一母线或泄漏通道波形标准差明显高。", "keywords": ["std", "range", "channel"], "related_causes": [3, "extra_1"]},
            {"name": "历史窗口缺少恢复趋势", "description": "历史点显示泄漏或母线偏移长期存在。", "keywords": ["history", "delta", "leakage"], "related_causes": [1, "extra_6"]},
            {"name": "恢复快照仍有残余泄漏", "description": "after_recovery 中泄漏未降回基线。", "keywords": ["after_recovery", "leakage"], "related_causes": ["extra_2", "extra_6"]},
            {"name": "母排电压峰峰值异常", "description": "母线最大/最小差值扩大，可能存在对地放电路径。", "keywords": ["max", "min", "range", "直流母线"], "related_causes": ["extra_5"]},
        ],
        "checks": ["分支绝缘电阻测试", "端子排和电缆头凝露检查", "IMD 零点和采样地校准", "负载支路逐段拉路定位", "母排绝缘套管外观检查", "柜内粉尘盐雾清洁复核"],
        "actions": ["隔离可疑支路后复测泄漏", "烘干并更换受潮端子密封", "修复破损电缆并重新绝缘", "重新校准绝缘监测回路", "更换压伤母排绝缘件", "设置观察期确认泄漏回落"],
        "risks": ["单点接地发展为两点短路", "柜体带电风险", "绝缘继续下降引发保护跳闸"],
    },
    "E03": {
        "extra_causes": [
            {"name": "ESR 升高", "description": "电容等效串联电阻增大导致纹波和温升同步上升。", "keywords": ["ripple", "std", "voltage", "temperature"]},
            {"name": "容量衰减", "description": "有效容量下降，负载阶跃时母线恢复变慢。", "keywords": ["delta", "voltage_pos", "voltage_neg"]},
            {"name": "母排连接松动", "description": "电容组母排螺栓松动造成局部发热和高频纹波。", "keywords": ["range", "std", "current"]},
            {"name": "散热风道堵塞", "description": "柜内热积累加速电容老化。", "keywords": ["temperature", "history"]},
            {"name": "预充回路异常", "description": "预充电阻或接触器异常使电容受冲击。", "keywords": ["突变", "after", "delta"]},
            {"name": "电容组不均流", "description": "并联电容参数离散导致单体过流。", "keywords": ["ripple", "current", "fft"]},
        ],
        "evidence_patterns": [
            {"name": "母线纹波长期升高", "description": "正负母线标准差和峰峰值持续偏高。", "keywords": ["voltage", "std", "range"], "related_causes": ["extra_1", "extra_2"]},
            {"name": "负载阶跃恢复慢", "description": "历史窗口中电压恢复斜率变慢。", "keywords": ["history", "delta", "current"], "related_causes": ["extra_2"]},
            {"name": "高频谱峰增强", "description": "FFT Top-K 中开关频率侧带增强。", "keywords": ["fft", "top_bin"], "related_causes": ["extra_1", "extra_6"]},
            {"name": "电压波动与电流相关", "description": "负载电流变化时母线波动同步扩大。", "keywords": ["current", "voltage"], "related_causes": ["extra_6"]},
            {"name": "恢复后纹波仍高", "description": "恢复快照中母线纹波未回落。", "keywords": ["after_recovery", "std"], "related_causes": ["extra_1"]},
            {"name": "突发大幅电压跌落", "description": "最大/最小值差异过大，需排除连接和预充冲击。", "keywords": ["range", "min", "max"], "related_causes": ["extra_3", "extra_5"]},
            {"name": "同一通道多次重复", "description": "同设备历史重复出现母线纹波异常。", "keywords": ["history", "points"], "related_causes": ["extra_1", "extra_4"]},
            {"name": "双母线波动不对称", "description": "正负母线纹波差异大，可能为单组电容异常。", "keywords": ["voltage_pos", "voltage_neg"], "related_causes": ["extra_6"]},
        ],
        "checks": ["电容 ESR/容量离线测试", "母排螺栓力矩复核", "电容外观鼓包漏液检查", "柜内温升和风道检查", "预充电阻和接触器动作检查", "并联电容均流复测"],
        "actions": ["更换 ESR 异常电容", "紧固母排并做热成像复测", "清理风道并恢复散热", "修复预充回路冲击源", "按组更换离散严重电容", "延长运行观察并记录纹波趋势"],
        "risks": ["电容热失控", "母线欠压/过压保护频繁动作", "纹波扩大影响功率模块寿命"],
    },
    "E04": {
        "extra_causes": [
            {"name": "键合线疲劳开裂", "description": "功率循环导致 IGBT 内部键合线开路。", "keywords": ["current", "std", "phase"]},
            {"name": "门极驱动欠压", "description": "驱动电源低于阈值，IGBT 无法可靠导通。", "keywords": ["pwm", "driver", "current"]},
            {"name": "门极电阻开路", "description": "门极回路断开导致单管无驱动。", "keywords": ["driver", "gate"]},
            {"name": "焊点热疲劳", "description": "模块焊层或端子焊点开裂，输出缺相。", "keywords": ["temperature", "current", "history"]},
            {"name": "退饱和保护误锁", "description": "保护电路误触发锁定驱动输出。", "keywords": ["desat", "fault", "pwm"]},
            {"name": "相电流采样漂移", "description": "采样链路漂移导致开路判断偏差。", "keywords": ["current", "采样", "reference"]},
        ],
        "evidence_patterns": [
            {"name": "负载电流缺相", "description": "负载电流均值或波动低于同工况预期。", "keywords": ["负载电流", "current", "min"], "related_causes": [1, "extra_1"]},
            {"name": "驱动存在但输出无响应", "description": "PWM/驱动正常而电流响应缺失。", "keywords": ["pwm", "current"], "related_causes": ["extra_2", "extra_3"]},
            {"name": "开关频率侧带异常", "description": "FFT 中开关频率相关峰值偏移。", "keywords": ["fft", "top_bin"], "related_causes": [2, "extra_5"]},
            {"name": "故障随温度或负载出现", "description": "历史窗口显示负载升高后异常更明显。", "keywords": ["history", "current", "delta"], "related_causes": ["extra_1", "extra_4"]},
            {"name": "电流突降后保护", "description": "电流突然下降并伴随保护状态。", "keywords": ["min", "delta", "fault"], "related_causes": ["extra_5"]},
            {"name": "单通道波动异常", "description": "电流通道标准差或范围异常。", "keywords": ["std", "range", "current"], "related_causes": ["extra_6"]},
            {"name": "恢复前后差异明显", "description": "恢复快照前后电流响应变化大。", "keywords": ["before_recovery", "after_recovery"], "related_causes": ["extra_2"]},
            {"name": "母线正常但输出异常", "description": "母线统计正常，输出电流异常更突出。", "keywords": ["voltage", "current"], "related_causes": ["extra_3", "extra_6"]},
        ],
        "checks": ["门极波形和驱动电源复测", "IGBT 模块端子压降测量", "门极电阻和回路连续性检查", "热像检查模块焊点和端子", "退饱和保护状态复核", "相电流采样零点校准"],
        "actions": ["隔离功率模块并做静态测试", "修复驱动电源欠压源", "更换开路门极电阻", "更换热疲劳模块或端子", "复位并验证保护锁定条件", "校准采样链路后复测"],
        "risks": ["单相过流扩大", "模块二次击穿", "逆变输出不平衡损伤负载"],
    },
    "E05": {
        "extra_causes": [
            {"name": "PE 连接松动", "description": "保护地连续性下降，母线对地参考漂移。", "keywords": ["ground", "pe", "leakage"]},
            {"name": "电缆进水", "description": "电缆头或桥架积水形成接地通路。", "keywords": ["湿气", "漏电流", "after_recovery"]},
            {"name": "金属异物搭接", "description": "柜内金属屑或工具残留搭接母排和地。", "keywords": ["突发", "range", "voltage"]},
            {"name": "绝缘支架碳化", "description": "长期爬电导致绝缘支架形成碳化通道。", "keywords": ["history", "leakage", "std"]},
            {"name": "SPD 对地击穿", "description": "浪涌器件对地短漏，表现为母线接地。", "keywords": ["spd", "ground", "leakage"]},
            {"name": "检修后接线错误", "description": "维护后母线或屏蔽层接线错误引入接地点。", "keywords": ["after", "突发", "history_delta"]},
        ],
        "evidence_patterns": [
            {"name": "漏电流与母线偏移同现", "description": "漏电流上升并伴随母线电压对地偏移。", "keywords": ["漏电流", "voltage", "ground"], "related_causes": [1, "extra_1"]},
            {"name": "接地故障单次尖峰", "description": "快照中出现短时高幅尖峰。", "keywords": ["max", "range", "突发"], "related_causes": ["extra_3"]},
            {"name": "恢复后仍有泄漏", "description": "恢复快照中泄漏路径仍未完全消失。", "keywords": ["after_recovery", "leakage"], "related_causes": ["extra_2", "extra_4"]},
            {"name": "母线正负不对称", "description": "正负母线对地统计明显不对称。", "keywords": ["voltage_pos", "voltage_neg"], "related_causes": [1, "extra_5"]},
            {"name": "历史趋势缓慢恶化", "description": "历史窗口泄漏或母线偏移缓慢增大。", "keywords": ["history", "delta", "leakage"], "related_causes": ["extra_4"]},
            {"name": "维护后立刻出现", "description": "故障与操作后时间点一致。", "keywords": ["after", "history_delta"], "related_causes": ["extra_6"]},
            {"name": "湿度相关反复", "description": "同一设备多次恢复/复发，疑似进水或凝露。", "keywords": ["before_recovery", "湿气"], "related_causes": ["extra_2"]},
            {"name": "PE 连续性异常嫌疑", "description": "漏电流异常但负载电流平稳。", "keywords": ["pe", "负载电流", "漏电流"], "related_causes": ["extra_1"]},
        ],
        "checks": ["PE 连续性和接地电阻测量", "电缆头进水和护套检查", "柜内金属异物清理", "绝缘支架碳化痕迹检查", "SPD 对地漏电测试", "检修接线复核"],
        "actions": ["恢复 PE 连接并复测", "烘干并重做电缆密封", "清理异物后绝缘复测", "更换碳化绝缘支架", "更换击穿 SPD", "按图纸纠正屏蔽和接地接线"],
        "risks": ["柜体触电风险", "两点接地短路", "接地保护越级跳闸"],
    },
    "E06": {
        "extra_causes": [
            {"name": "死区时间配置异常", "description": "控制参数异常导致上下桥臂驱动时序异常。", "keywords": ["pwm", "deadtime", "history"]},
            {"name": "隔离驱动器老化", "description": "隔离器边沿延迟或丢脉冲，PWM 变形。", "keywords": ["driver", "pwm", "fft"]},
            {"name": "驱动电源 UVLO", "description": "驱动电源欠压保护反复进入。", "keywords": ["uvlo", "driver", "突发"]},
            {"name": "控制时钟抖动", "description": "时钟源异常导致 PWM 频率漂移。", "keywords": ["fft", "top_bin", "frequency"]},
            {"name": "采样反馈相位错误", "description": "反馈采样相位与 PWM 更新不同步。", "keywords": ["采样", "current", "voltage"]},
            {"name": "强 EMI 干扰", "description": "强干扰导致控制链路误触发或丢脉冲。", "keywords": ["emi", "std", "range"]},
        ],
        "evidence_patterns": [
            {"name": "开关频率漂移", "description": "FFT Top-K 显示开关频率或侧带位置异常。", "keywords": ["fft", "top_bin", "frequency"], "related_causes": ["extra_4"]},
            {"name": "输出与命令不一致", "description": "电流/电压响应与预期 PWM 命令不一致。", "keywords": ["current", "voltage", "pwm"], "related_causes": ["extra_5"]},
            {"name": "通道标准差突增", "description": "某通道波形标准差或范围突增，疑似驱动抖动。", "keywords": ["std", "range"], "related_causes": ["extra_2", "extra_6"]},
            {"name": "故障间歇出现", "description": "恢复前后差异明显，符合干扰或欠压保护。", "keywords": ["before_recovery", "after_recovery"], "related_causes": ["extra_3", "extra_6"]},
            {"name": "母线正常但输出异常", "description": "母线稳定而输出侧波形异常。", "keywords": ["voltage", "current"], "related_causes": ["extra_1", "extra_5"]},
            {"name": "历史窗口频繁跳变", "description": "历史字段出现短时间高频变化。", "keywords": ["history", "delta"], "related_causes": ["extra_4", "extra_6"]},
            {"name": "负载变化触发", "description": "负载电流变化后 PWM 异常更明显。", "keywords": ["负载电流", "current"], "related_causes": ["extra_5"]},
            {"name": "单次尖峰扰动", "description": "最大/最小范围出现孤立尖峰。", "keywords": ["max", "min", "range"], "related_causes": ["extra_6"]},
        ],
        "checks": ["PWM 频率/占空比/死区示波复核", "隔离驱动器输入输出边沿对比", "驱动电源 UVLO 门限检查", "控制时钟源和同步信号检查", "采样触发相位和校准检查", "EMI 屏蔽和接地复核"],
        "actions": ["恢复控制参数并锁定版本", "更换老化隔离驱动器", "修复驱动电源欠压源", "更换异常时钟源或同步链路", "重新校准采样触发相位", "增加屏蔽/接地并复测扰动"],
        "risks": ["上下桥臂误导通", "输出谐波超限", "驱动链路误动作损坏功率器件"],
    },
}

FAULT_KNOWLEDGE_GRAPH = {
    "E01": {
        "name": "交流窜入 (AC Intrusion)",
        "root_cause": "整流二极管击穿",
        "solution": "更换整流桥臂，检查滤波电容",
        "root_causes": [
            {"name": "逆变器滤波器故障", "description": "逆变器输出滤波器损坏导致AC信号泄漏到DC总线"},
            {"name": "隔离变压器失效", "description": "DC/AC隔离变压器绝缘层破损"},
            {"name": "接地系统异常", "description": "接地电阻增大或接地线松动"}
        ],
        "solutions": [
            {"name": "检查电容C1", "description": "检查并更换逆变器输出滤波器中的C1电容"},
            {"name": "测试隔离变压器", "description": "使用兆欧表测试变压器绝缘电阻"},
            {"name": "重新连接接地线", "description": "检查并紧固接地线连接，确保接地电阻<1Ω"}
        ],
        "detailed_report": """【AI 深度诊断报告】
故障定性：直流母线交流分量异常（纹波系数 > 5%）。
--------------------------------------------------
1. 机理分析 (Failure Mechanism):
系统监测到直流母线电压中叠加了显著的工频(50Hz)及三次谐波(150Hz)分量。频谱特征分析表明，整流侧与直流侧之间的物理隔离特性已失效。正常情况下，直流母线应仅包含微量高频开关噪声，当前出现的低频大振幅波动通常是由于三相整流桥中至少有一个桥臂的二极管发生反向雪崩击穿，或者是直流侧平波电抗器饱和失效，导致交流电压直接"骑"在直流电压之上。

2. 风险评估 (Risk Assessment):
- 极高风险：交流分量会导致直流负载（如逆变器、DC/DC变换器）输入端的电解电容反复充放电，产生巨大的内部热耗（I²R），极易引发电容爆浆或起火。
- 继电保护误动：波动的电压有效值可能触发欠压或过压保护装置，导致非计划停机。

3. 智能运维建议 (Actionable Advice):
- [立即] 停机并断开交流输入侧断路器，使用万用表二极管档位测量整流桥六个臂的正反向压降，定位击穿元件。
- [检查] 检查直流母线滤波电容是否有鼓包、漏液现象，交流窜入通常伴随电容过热。
- [预防] 建议加装交流侧浪涌保护器(SPD)，防止电网侧操作过电压再次击穿整流元件。"""
    },
    "AC_INTRUSION": {
        "name": "交流窜入",
        "root_cause": "整流二极管击穿",
        "solution": "更换整流桥臂，检查滤波电容",
        "root_causes": [
            {"name": "逆变器滤波器故障", "description": "逆变器输出滤波器损坏导致AC信号泄漏到DC总线"},
            {"name": "隔离变压器失效", "description": "DC/AC隔离变压器绝缘层破损"},
            {"name": "接地系统异常", "description": "接地电阻增大或接地线松动"}
        ],
        "solutions": [
            {"name": "检查电容C1", "description": "检查并更换逆变器输出滤波器中的C1电容"},
            {"name": "测试隔离变压器", "description": "使用兆欧表测试变压器绝缘电阻"},
            {"name": "重新连接接地线", "description": "检查并紧固接地线连接，确保接地电阻<1Ω"}
        ],
        "detailed_report": """【AI 深度诊断报告】
故障定性：直流母线交流分量异常（纹波系数 > 5%）。
--------------------------------------------------
1. 机理分析 (Failure Mechanism):
系统监测到直流母线电压中叠加了显著的工频(50Hz)及三次谐波(150Hz)分量。频谱特征分析表明，整流侧与直流侧之间的物理隔离特性已失效。正常情况下，直流母线应仅包含微量高频开关噪声，当前出现的低频大振幅波动通常是由于三相整流桥中至少有一个桥臂的二极管发生反向雪崩击穿，或者是直流侧平波电抗器饱和失效，导致交流电压直接"骑"在直流电压之上。

2. 风险评估 (Risk Assessment):
- 极高风险：交流分量会导致直流负载（如逆变器、DC/DC变换器）输入端的电解电容反复充放电，产生巨大的内部热耗（I²R），极易引发电容爆浆或起火。
- 继电保护误动：波动的电压有效值可能触发欠压或过压保护装置，导致非计划停机。

3. 智能运维建议 (Actionable Advice):
- [立即] 停机并断开交流输入侧断路器，使用万用表二极管档位测量整流桥六个臂的正反向压降，定位击穿元件。
- [检查] 检查直流母线滤波电容是否有鼓包、漏液现象，交流窜入通常伴随电容过热。
- [预防] 建议加装交流侧浪涌保护器(SPD)，防止电网侧操作过电压再次击穿整流元件。"""
    },
    "E02": {
        "name": "绝缘故障 (Insulation Fault)",
        "root_cause": "对地绝缘电阻下降",
        "solution": "使用电桥法定位接地点",
        "root_causes": [
            {"name": "绝缘老化", "description": "电缆或设备绝缘层老化导致漏电"},
            {"name": "湿气侵入", "description": "设备密封失效，湿气侵入导致绝缘下降"},
            {"name": "机械损伤", "description": "电缆被外力损伤导致绝缘破损"}
        ],
        "solutions": [
            {"name": "绝缘测试", "description": "使用绝缘测试仪检测所有DC电缆绝缘电阻"},
            {"name": "检查密封", "description": "检查设备外壳密封条，更换密封件"},
            {"name": "修复电缆", "description": "定位并修复受损电缆，重新绝缘处理"}
        ],
        "detailed_report": """【AI 深度诊断报告】
故障定性：直流系统绝缘阻抗过低（< 20kΩ）。
--------------------------------------------------
1. 机理分析 (Failure Mechanism):
IMD（绝缘监测设备）检测到正极或负极对地绝缘电阻显著低于安全阈值。直流系统通常采用IT接地制（不接地系统），允许单点接地运行，但当前状态表明系统已丧失悬浮特性。这通常由以下原因引起：电缆外皮老化开裂导致线芯接触线槽金属壁；户外接线盒密封失效进水；或风机机舱内凝露导致绝缘子表面形成导电水膜。

2. 风险评估 (Risk Assessment):
- 目前处于"单点接地"状态，虽暂时不会产生大电流，但若系统中出现第二点接地，将立刻形成"两点接地短路"。
- 短路电流将不经过负载直接流经故障点，释放巨大能量，极大概率引燃电缆或烧毁柜体，并可能导致上级保护越级跳闸，扩大停电范围。

3. 智能运维建议 (Actionable Advice):
- [排查] 启用支路绝缘选线功能，通过拉路法或注入低频信号法，锁定具体故障支路。
- [环境] 检查近期是否有雨雪天气，重点排查户外端子箱、电缆沟等易积水区域。
- [处置] 发现破损电缆后，应立即制作中间接头或更换整段电缆，严禁仅用绝缘胶布简单包扎处理。"""
    },
    "INSULATION_FAULT": {
        "name": "绝缘故障",
        "root_cause": "对地绝缘电阻下降",
        "solution": "使用电桥法定位接地点",
        "root_causes": [
            {"name": "绝缘老化", "description": "电缆或设备绝缘层老化导致漏电"},
            {"name": "湿气侵入", "description": "设备密封失效，湿气侵入导致绝缘下降"},
            {"name": "机械损伤", "description": "电缆被外力损伤导致绝缘破损"}
        ],
        "solutions": [
            {"name": "绝缘测试", "description": "使用绝缘测试仪检测所有DC电缆绝缘电阻"},
            {"name": "检查密封", "description": "检查设备外壳密封条，更换密封件"},
            {"name": "修复电缆", "description": "定位并修复受损电缆，重新绝缘处理"}
        ],
        "detailed_report": """【AI 深度诊断报告】
故障定性：直流系统绝缘阻抗过低（< 20kΩ）。
--------------------------------------------------
1. 机理分析 (Failure Mechanism):
IMD（绝缘监测设备）检测到正极或负极对地绝缘电阻显著低于安全阈值。直流系统通常采用IT接地制（不接地系统），允许单点接地运行，但当前状态表明系统已丧失悬浮特性。这通常由以下原因引起：电缆外皮老化开裂导致线芯接触线槽金属壁；户外接线盒密封失效进水；或风机机舱内凝露导致绝缘子表面形成导电水膜。

2. 风险评估 (Risk Assessment):
- 目前处于"单点接地"状态，虽暂时不会产生大电流，但若系统中出现第二点接地，将立刻形成"两点接地短路"。
- 短路电流将不经过负载直接流经故障点，释放巨大能量，极大概率引燃电缆或烧毁柜体，并可能导致上级保护越级跳闸，扩大停电范围。

3. 智能运维建议 (Actionable Advice):
- [排查] 启用支路绝缘选线功能，通过拉路法或注入低频信号法，锁定具体故障支路。
- [环境] 检查近期是否有雨雪天气，重点排查户外端子箱、电缆沟等易积水区域。
- [处置] 发现破损电缆后，应立即制作中间接头或更换整段电缆，严禁仅用绝缘胶布简单包扎处理。"""
    },
    "E03": {
        "name": "电容老化 (Capacitor Aging)",
        "root_cause": "ESR值升高",
        "solution": "更换同批次电容组",
        "root_causes": [
            {"name": "电解液干涸", "description": "电解电容内部电解液因高温或老化导致干涸，容量降低"},
            {"name": "环境过温", "description": "电容器工作环境温度长期过高，加速老化过程"},
            {"name": "纹波过流", "description": "高频纹波电流过大，导致ESR（等效串联电阻）增大"}
        ],
        "solutions": [
            {"name": "检测ESR值", "description": "使用LCR表测量电容器ESR，若超过规格值2倍需更换"},
            {"name": "更换电容模组", "description": "更换直流母线滤波电容器模组，确保容量和ESR符合要求"}
        ],
        "detailed_report": """【AI 深度诊断报告】
故障定性：直流支撑电容健康度低（老化指数 > 80%）。
--------------------------------------------------
1. 机理分析 (Failure Mechanism):
基于纹波电流与温升模型分析，监测到电容组的等效串联电阻(ESR)异常升高，且容量(C)呈现衰减趋势。电解电容内部电解液随时间逐渐挥发干涸，导致离子导电能力下降，ESR升高。在相同的纹波电流下，升高的ESR会产生更多热量，进一步加速电解液挥发，形成"温升-老化"的正反馈恶性循环。

2. 风险评估 (Risk Assessment):
- 滤波失效：母线电压纹波增大，影响逆变器输出电能质量。
- 炸机风险：电容内部压力过大可能顶开防爆阀，严重时发生喷液或爆炸，腐蚀周围电路板及器件。

3. 智能运维建议 (Actionable Advice):
- [检测] 停机放电后，使用LCR数字电桥抽检电容单体的100Hz/1kHz下的ESR值，对比出厂规格书。
- [更换] 务必成组更换！严禁新旧电容混用，否则新电容会因分流阻抗更小而承担大部分纹波电流，导致过早失效。
- [散热] 检查风冷/水冷散热通道是否堵塞，降低环境温度可显著延缓电容老化进程。"""
    },
    "DC_CAPACITOR_AGING": {
        "name": "直流母线电容老化",
        "root_cause": "ESR值升高",
        "solution": "更换同批次电容组",
        "root_causes": [
            {"name": "电解液干涸", "description": "电解电容内部电解液因高温或老化导致干涸，容量降低"},
            {"name": "环境过温", "description": "电容器工作环境温度长期过高，加速老化过程"},
            {"name": "纹波过流", "description": "高频纹波电流过大，导致ESR（等效串联电阻）增大"}
        ],
        "solutions": [
            {"name": "检测ESR值", "description": "使用LCR表测量电容器ESR，若超过规格值2倍需更换"},
            {"name": "更换电容模组", "description": "更换直流母线滤波电容器模组，确保容量和ESR符合要求"}
        ],
        "detailed_report": """【AI 深度诊断报告】
故障定性：直流支撑电容健康度低（老化指数 > 80%）。
--------------------------------------------------
1. 机理分析 (Failure Mechanism):
基于纹波电流与温升模型分析，监测到电容组的等效串联电阻(ESR)异常升高，且容量(C)呈现衰减趋势。电解电容内部电解液随时间逐渐挥发干涸，导致离子导电能力下降，ESR升高。在相同的纹波电流下，升高的ESR会产生更多热量，进一步加速电解液挥发，形成"温升-老化"的正反馈恶性循环。

2. 风险评估 (Risk Assessment):
- 滤波失效：母线电压纹波增大，影响逆变器输出电能质量。
- 炸机风险：电容内部压力过大可能顶开防爆阀，严重时发生喷液或爆炸，腐蚀周围电路板及器件。

3. 智能运维建议 (Actionable Advice):
- [检测] 停机放电后，使用LCR数字电桥抽检电容单体的100Hz/1kHz下的ESR值，对比出厂规格书。
- [更换] 务必成组更换！严禁新旧电容混用，否则新电容会因分流阻抗更小而承担大部分纹波电流，导致过早失效。
- [散热] 检查风冷/水冷散热通道是否堵塞，降低环境温度可显著延缓电容老化进程。"""
    },
    "E04": {
        "name": "IGBT开路 (IGBT Open Circuit)",
        "root_cause": "栅极驱动失效或键合线断裂",
        "solution": "检测驱动板波形",
        "root_causes": [
            {"name": "热应力疲劳", "description": "IGBT长期工作在高温下，热循环导致键合线断裂"},
            {"name": "驱动电路失效", "description": "门极驱动电路故障，无法提供正确的驱动信号"},
            {"name": "过流冲击", "description": "负载突变或短路导致过流，烧毁IGBT芯片"}
        ],
        "solutions": [
            {"name": "检查门极驱动", "description": "使用示波器检查IGBT门极驱动波形，确认驱动电路正常"},
            {"name": "更换IGBT模块", "description": "更换故障IGBT模块，确保新模块参数匹配"}
        ],
        "detailed_report": """【AI 深度诊断报告】
故障定性：变流器桥臂功率器件开路故障。
--------------------------------------------------
1. 机理分析 (Failure Mechanism):
输出电流波形出现严重的非对称畸变，正半周或负半周缺失（削顶效应），且频谱中偶次谐波含量激增。这表明逆变桥中某一只IGBT管未能随PWM信号导通。可能原因包括：栅极驱动板供电异常（如+15V/-9V丢失）、驱动光耦老化延迟、或IGBT模块内部铝键合线因热疲劳而熔断脱落。

2. 风险评估 (Risk Assessment):
- 严重失衡：系统被迫进入非全相运行状态，导致直流母线中点电位剧烈波动。
- 连锁损坏：同一桥臂的另一只对管将承受倍增的电流应力，极易在短时间内发生过流烧毁（炸管）。

3. 智能运维建议 (Actionable Advice):
- [诊断] 示波器测量故障相上下桥臂的栅极-发射极(G-E)驱动波形，确认是否有正常的PWM脉冲。
- [测试] 静态测量IGBT集电极-发射极(C-E)阻抗，判断是否内部断路。
- [处置] 更换损坏模块时，必须重新涂抹导热硅脂，并使用力矩扳手按标准力矩紧固，防止接触热阻过大再次损坏。"""
    },
    "IGBT_OPEN_CIRCUIT": {
        "name": "变流器IGBT开路",
        "root_cause": "栅极驱动失效或键合线断裂",
        "solution": "检测驱动板波形",
        "root_causes": [
            {"name": "热应力疲劳", "description": "IGBT长期工作在高温下，热循环导致键合线断裂"},
            {"name": "驱动电路失效", "description": "门极驱动电路故障，无法提供正确的驱动信号"},
            {"name": "过流冲击", "description": "负载突变或短路导致过流，烧毁IGBT芯片"}
        ],
        "solutions": [
            {"name": "检查门极驱动", "description": "使用示波器检查IGBT门极驱动波形，确认驱动电路正常"},
            {"name": "更换IGBT模块", "description": "更换故障IGBT模块，确保新模块参数匹配"}
        ],
        "detailed_report": """【AI 深度诊断报告】
故障定性：变流器桥臂功率器件开路故障。
--------------------------------------------------
1. 机理分析 (Failure Mechanism):
输出电流波形出现严重的非对称畸变，正半周或负半周缺失（削顶效应），且频谱中偶次谐波含量激增。这表明逆变桥中某一只IGBT管未能随PWM信号导通。可能原因包括：栅极驱动板供电异常（如+15V/-9V丢失）、驱动光耦老化延迟、或IGBT模块内部铝键合线因热疲劳而熔断脱落。

2. 风险评估 (Risk Assessment):
- 严重失衡：系统被迫进入非全相运行状态，导致直流母线中点电位剧烈波动。
- 连锁损坏：同一桥臂的另一只对管将承受倍增的电流应力，极易在短时间内发生过流烧毁（炸管）。

3. 智能运维建议 (Actionable Advice):
- [诊断] 示波器测量故障相上下桥臂的栅极-发射极(G-E)驱动波形，确认是否有正常的PWM脉冲。
- [测试] 静态测量IGBT集电极-发射极(C-E)阻抗，判断是否内部断路。
- [处置] 更换损坏模块时，必须重新涂抹导热硅脂，并使用力矩扳手按标准力矩紧固，防止接触热阻过大再次损坏。"""
    },
    "E05": {
        "name": "接地故障 (Grounding Fault)",
        "root_cause": "金属性接地",
        "solution": "立即停机排查",
        "root_causes": [
            {"name": "电缆破损", "description": "DC母线电缆绝缘层破损，导体接触地线或机壳"},
            {"name": "接头受潮", "description": "电缆接头处受潮导致绝缘下降，形成接地通路"},
            {"name": "金属异物", "description": "金属异物（如螺丝、工具）落入设备，造成短路接地"}
        ],
        "solutions": [
            {"name": "拉路排查", "description": "采用拉路法逐一断开各分支，定位接地故障点"},
            {"name": "检查绝缘层", "description": "检查所有DC母线电缆和接头的绝缘层完整性，更换受损部件"}
        ],
        "detailed_report": """【AI 深度诊断报告】
故障定性：直流母线发生直接金属性接地（Dead Earth）。
--------------------------------------------------
1. 机理分析 (Failure Mechanism):
电压监测显示，正极对地电压趋近于0V（或负极对地电压趋近于0V），而另一极对地电压升高至线电压水平。这与E02（绝缘下降）不同，E05通常代表绝缘层完全被击穿，导体直接接触到了金属机壳或接地排。常见于剧烈震动导致的线缆磨损、老鼠咬噬破坏或金属工具遗落在母排上。

2. 风险评估 (Risk Assessment):
- 人身安全：此时机壳可能带有高电位（如果接地不良），对巡检人员构成致命触电威胁。
- 设备损毁：系统处于极其脆弱的临界状态，任何扰动都可能引发弧光短路，释放出的电弧能量足以瞬间气化铜排并炸毁柜体。

3. 智能运维建议 (Actionable Advice):
- [紧急] 立即切断系统主输入电源！严禁带电进行任何物理检查。
- [排查] 使用兆欧表（摇表）分段测量母线及支路对地电阻，寻找电阻为零的接地点。
- [警示] 在故障彻底排除前，严禁再次合闸试送电，防止发生二次爆炸事故。"""
    },
    "DC_BUS_GROUNDING": {
        "name": "直流母线接地故障",
        "root_cause": "金属性接地",
        "solution": "立即停机排查",
        "root_causes": [
            {"name": "电缆破损", "description": "DC母线电缆绝缘层破损，导体接触地线或机壳"},
            {"name": "接头受潮", "description": "电缆接头处受潮导致绝缘下降，形成接地通路"},
            {"name": "金属异物", "description": "金属异物（如螺丝、工具）落入设备，造成短路接地"}
        ],
        "solutions": [
            {"name": "拉路排查", "description": "采用拉路法逐一断开各分支，定位接地故障点"},
            {"name": "检查绝缘层", "description": "检查所有DC母线电缆和接头的绝缘层完整性，更换受损部件"}
        ],
        "detailed_report": """【AI 深度诊断报告】
故障定性：直流母线发生直接金属性接地（Dead Earth）。
--------------------------------------------------
1. 机理分析 (Failure Mechanism):
电压监测显示，正极对地电压趋近于0V（或负极对地电压趋近于0V），而另一极对地电压升高至线电压水平。这与E02（绝缘下降）不同，E05通常代表绝缘层完全被击穿，导体直接接触到了金属机壳或接地排。常见于剧烈震动导致的线缆磨损、老鼠咬噬破坏或金属工具遗落在母排上。

2. 风险评估 (Risk Assessment):
- 人身安全：此时机壳可能带有高电位（如果接地不良），对巡检人员构成致命触电威胁。
- 设备损毁：系统处于极其脆弱的临界状态，任何扰动都可能引发弧光短路，释放出的电弧能量足以瞬间气化铜排并炸毁柜体。

3. 智能运维建议 (Actionable Advice):
- [紧急] 立即切断系统主输入电源！严禁带电进行任何物理检查。
- [排查] 使用兆欧表（摇表）分段测量母线及支路对地电阻，寻找电阻为零的接地点。
- [警示] 在故障彻底排除前，严禁再次合闸试送电，防止发生二次爆炸事故。"""
    },
    "E06": {
        "name": "PWM异常 (PWM Abnormality)",
        "root_cause": "PWM调制或驱动链路异常",
        "solution": "检查PWM控制、驱动板和采样反馈链路",
        "root_causes": [
            {"name": "PWM占空比抖动", "description": "控制器调制输出存在异常抖动或周期性扰动"},
            {"name": "驱动脉冲丢失", "description": "门极驱动链路存在缺脉冲、延迟或干扰"},
            {"name": "采样反馈异常", "description": "电流/电压反馈噪声导致控制环路输出异常"}
        ],
        "solutions": [
            {"name": "检查PWM波形", "description": "使用示波器确认PWM频率、占空比和互补死区是否稳定"},
            {"name": "检查驱动板", "description": "检查门极驱动供电、光耦/隔离器和驱动电阻"},
            {"name": "检查采样反馈", "description": "排查ADC采样、滤波和电流反馈线路的噪声或接触问题"}
        ],
        "detailed_report": """【AI 深度诊断报告】
故障定性：PWM控制异常。
--------------------------------------------------
1. 机理分析 (Failure Mechanism):
监测到直流母线或负载电流中存在与开关控制相关的异常高频分量、占空比抖动、缺脉冲或边带能量升高。这通常指向PWM调制链路、门极驱动链路或采样反馈链路的异常。

2. 风险评估 (Risk Assessment):
- 输出质量下降：PWM异常会引入额外纹波、谐波和电流调制。
- 器件应力升高：持续缺脉冲或抖动可能导致桥臂电流不均衡，增加功率器件热应力。

3. 智能运维建议 (Actionable Advice):
- [诊断] 使用示波器同时观察PWM控制信号、门极驱动信号和负载电流波形。
- [排查] 检查驱动板供电、隔离器件、采样反馈线路和控制参数。
- [处置] 若确认存在缺脉冲或异常抖动，应先停机排查驱动链路，避免进一步损伤功率器件。"""
    },
    "PWM_ABNORMAL": {
        "name": "PWM异常",
        "root_cause": "PWM调制或驱动链路异常",
        "solution": "检查PWM控制、驱动板和采样反馈链路",
        "root_causes": [
            {"name": "PWM占空比抖动", "description": "控制器调制输出存在异常抖动或周期性扰动"},
            {"name": "驱动脉冲丢失", "description": "门极驱动链路存在缺脉冲、延迟或干扰"},
            {"name": "采样反馈异常", "description": "电流/电压反馈噪声导致控制环路输出异常"}
        ],
        "solutions": [
            {"name": "检查PWM波形", "description": "使用示波器确认PWM频率、占空比和互补死区是否稳定"},
            {"name": "检查驱动板", "description": "检查门极驱动供电、光耦/隔离器和驱动电阻"},
            {"name": "检查采样反馈", "description": "排查ADC采样、滤波和电流反馈线路的噪声或接触问题"}
        ],
        "detailed_report": """【AI 深度诊断报告】
故障定性：PWM控制异常。
--------------------------------------------------
1. 机理分析 (Failure Mechanism):
监测到直流母线或负载电流中存在与开关控制相关的异常高频分量、占空比抖动、缺脉冲或边带能量升高。这通常指向PWM调制链路、门极驱动链路或采样反馈链路的异常。

2. 风险评估 (Risk Assessment):
- 输出质量下降：PWM异常会引入额外纹波、谐波和电流调制。
- 器件应力升高：持续缺脉冲或抖动可能导致桥臂电流不均衡，增加功率器件热应力。

3. 智能运维建议 (Actionable Advice):
- [诊断] 使用示波器同时观察PWM控制信号、门极驱动信号和负载电流波形。
- [排查] 检查驱动板供电、隔离器件、采样反馈线路和控制参数。
- [处置] 若确认存在缺脉冲或异常抖动，应先停机排查驱动链路，避免进一步损伤功率器件。"""
    },
    "OVERVOLTAGE": {
        "name": "过电压",
        "root_causes": [
            {"name": "负载突然断开", "description": "大负载突然断开导致DC母线电压瞬间升高"},
            {"name": "稳压器故障", "description": "DC稳压器反馈回路异常"},
            {"name": "电容器失效", "description": "DC母线滤波电容器容量衰减或开路"}
        ],
        "solutions": [
            {"name": "检查负载连接", "description": "检查负载连接器是否松动，避免突然断开"},
            {"name": "更换稳压器IC", "description": "更换DC稳压器控制IC U1"},
            {"name": "更换滤波电容", "description": "更换DC母线滤波电容器组C2-C5"}
        ]
    },
    "UNDERVOLTAGE": {
        "name": "欠电压",
        "root_causes": [
            {"name": "输入电源不足", "description": "风电机组输出功率不足"},
            {"name": "电缆接触不良", "description": "DC输入电缆连接器氧化或松动"},
            {"name": "保护继电器跳闸", "description": "过流保护继电器动作"}
        ],
        "solutions": [
            {"name": "检查风电输出", "description": "检查风电机组输出功率和叶片状态"},
            {"name": "清洁连接器", "description": "清洁DC输入连接器，重新紧固"},
            {"name": "复位保护继电器", "description": "检查负载电流，复位过流保护继电器"}
        ]
    },
    "E00": {
        "name": "系统正常 (Normal)",
        "root_cause": "无",
        "solution": "周期性巡检",
        "root_causes": [],
        "solutions": [],
        "detailed_report": """【AI 智能诊断报告】
系统状态：健康运行中。
--------------------------------------------------
1. 状态分析:
当前直流母线电压、纹波系数、绝缘电阻及温度指标均在额定范围内。频谱分析未发现特征谐波，波形平滑稳定。边缘计算节点心跳正常，数据传输链路通畅。

2. 建议:
- 保持现有的周期性巡检计划。
- 关注环境温度变化对散热系统的影响。
- 定期清理控制柜进风口滤网积尘。"""
    }
}


# ==================== 知识图谱处理函数 ====================

def generate_ai_report(fault_code):
    """
    生成详细的AI智能诊断报告
    
    Args:
        fault_code: 故障代码 (E01-E05) 或故障类型名称 (AC_INTRUSION, INSULATION_FAULT等)
        
    Returns:
        str: 格式化的详细诊断报告
    """
    # 尝试直接查找（支持E01-E05格式）
    knowledge = FAULT_KNOWLEDGE_GRAPH.get(fault_code)
    
    # 如果未找到，尝试通过FAULT_CODE_MAP转换
    if not knowledge and fault_code in FAULT_CODE_MAP:
        mapped_code = FAULT_CODE_MAP[fault_code]
        if mapped_code:
            knowledge = FAULT_KNOWLEDGE_GRAPH.get(mapped_code)
    
    # 如果仍未找到，使用E00作为默认
    if not knowledge:
        knowledge = FAULT_KNOWLEDGE_GRAPH.get("E00", {})
    
    # 优先使用预写的详细报告
    if "detailed_report" in knowledge:
        return knowledge["detailed_report"]
    
    # 降级处理：使用简短字段生成基本报告
    fault_name = knowledge.get('name', '未知故障')
    root_cause = knowledge.get('root_cause', '未知')
    solution = knowledge.get('solution', '请联系技术支持')
    
    return f"""【AI 诊断】
故障类型：{fault_name}
根本原因：{root_cause}
建议方案：{solution}
生成时间：{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}
"""


def _resolve_fault_knowledge(fault_code: str | None) -> tuple[str | None, dict[str, Any] | None]:
    code = str(fault_code or "").strip()
    if code in FAULT_KNOWLEDGE_GRAPH:
        info = FAULT_KNOWLEDGE_GRAPH[code]
        if "root_causes" in info:
            return code, info
        mapped_code = FAULT_CODE_MAP.get(code)
        if mapped_code and mapped_code in FAULT_KNOWLEDGE_GRAPH:
            return mapped_code, FAULT_KNOWLEDGE_GRAPH[mapped_code]
        return None, None

    mapped_code = FAULT_CODE_MAP.get(code)
    if mapped_code is None:
        return None, None
    if mapped_code in FAULT_KNOWLEDGE_GRAPH:
        return mapped_code, FAULT_KNOWLEDGE_GRAPH[mapped_code]
    return None, None


def get_fault_reasoning_knowledge(fault_code: str | None) -> dict[str, Any]:
    code, _info = _resolve_fault_knowledge(fault_code)
    if not code:
        return {}
    mapped_code = FAULT_CODE_MAP.get(code) if code in FAULT_CODE_MAP else code
    return FAULT_REASONING_KB.get(code) or FAULT_REASONING_KB.get(str(mapped_code or "")) or {}


def _legacy_fault_knowledge_graph_unused(fault_code):
    """
    根据故障代码获取知识图谱数据（ECharts力导向图格式）
    
    Args:
        fault_code: 故障代码 (E01, E02) 或故障类型名称 (AC_INTRUSION, INSULATION_FAULT)
        
    Returns:
        dict: 包含nodes, links, categories的知识图谱数据，如果无效则返回None
    """
    original_code = fault_code
    fault_code, fault_info = _resolve_fault_knowledge(fault_code)
    if not fault_code or not fault_info:
        return None
    
    # 构建节点
    nodes = [
        {
            "id": fault_code,
            "name": fault_info["name"],
            "category": 0,  # 0=故障（红色）
            "symbolSize": 80,
            "value": fault_info["name"],
            "node_type": "fault",
            "source": "static_kg",
            "confidence": 1.0,
            "risk_level": None,
            "evidence_refs": [],
            "status": "static",
        }
    ]
    
    # 构建连接关系
    links = []
    node_id_counter = 1
    
    # 添加根本原因节点（黄色）
    for cause in fault_info["root_causes"]:
        cause_id = f"{fault_code}_cause_{node_id_counter}"
        nodes.append({
            "id": cause_id,
            "name": cause["name"],
            "category": 1,  # 1=根本原因（黄色）
            "symbolSize": 60,
            "value": cause["description"],
            "node_type": "cause",
            "source": "static_kg",
            "confidence": None,
            "risk_level": None,
            "evidence_refs": [],
            "status": "static",
        })
        links.append({
            "source": fault_code,
            "target": cause_id,
            "value": "原因",
            "relation": "has_cause",
            "confidence": None,
            "weight": 1.0,
            "evidence_refs": [],
            "explanation": cause["description"],
        })
        node_id_counter += 1
    
    # 添加解决方案节点（绿色）
    for solution in fault_info["solutions"]:
        solution_id = f"{fault_code}_solution_{node_id_counter}"
        nodes.append({
            "id": solution_id,
            "name": solution["name"],
            "category": 2,  # 2=解决方案（绿色）
            "symbolSize": 60,
            "value": solution["description"],
            "node_type": "solution",
            "source": "static_kg",
            "confidence": None,
            "risk_level": None,
            "evidence_refs": [],
            "status": "static",
        })
        links.append({
            "source": fault_code,
            "target": solution_id,
            "value": "解决方案",
            "relation": "mitigated_by",
            "confidence": None,
            "weight": 1.0,
            "evidence_refs": [],
            "explanation": solution["description"],
        })
        node_id_counter += 1
    
    return {
        "schema_version": REASONING_GRAPH_SCHEMA_VERSION,
        "fault_code": fault_code,
        "original_fault_code": original_code,
        "nodes": nodes,
        "links": links,
        "categories": GRAPH_CATEGORIES[:3],
        "reasoning": {
            "mode": "static",
            "status": "static",
            "summary": "本地图谱，尚未叠加 DeepSeek 推理结果。",
        },
        "evidence": [],
    }


def _canonical_fault_code(code: str | None) -> str | None:
    if not code:
        return None
    text = str(code)
    if text in EXPERT_KNOWLEDGE_LIBRARY:
        return text
    for standard_code, mapped_code in FAULT_CODE_MAP.items():
        if mapped_code == text:
            return standard_code
    return text


def _expert_items(items: Any) -> list[dict[str, Any]]:
    result = []
    for item in items or []:
        if isinstance(item, dict):
            result.append(dict(item))
        elif isinstance(item, (list, tuple)) and item:
            result.append({
                "name": str(item[0]),
                "description": str(item[1]) if len(item) > 1 else str(item[0]),
                "keywords": list(item[2]) if len(item) > 2 and isinstance(item[2], (list, tuple)) else [],
            })
        elif item:
            result.append({"name": str(item), "description": str(item), "keywords": []})
    return result


def _add_graph_node(nodes: list[dict[str, Any]], *, node_id: str, name: str, category: int,
                    node_type: str, value: str, symbol_size: int, source: str = "expert_kg",
                    keywords: list[str] | None = None, related_node_ids: list[str] | None = None) -> None:
    nodes.append({
        "id": node_id,
        "name": name,
        "category": category,
        "symbolSize": symbol_size,
        "value": value,
        "description": value,
        "node_type": node_type,
        "source": source,
        "confidence": None,
        "risk_level": None,
        "evidence_refs": [],
        "status": "static",
        "keywords": keywords or [],
        "related_node_ids": related_node_ids or [],
    })


def _add_graph_link(links: list[dict[str, Any]], source: str, target: str, *,
                    value: str, relation: str, explanation: str = "", confidence: float | None = None,
                    evidence_refs: list[str] | None = None, style: dict[str, Any] | None = None) -> None:
    if not source or not target or source == target:
        return
    if any(str(link.get("source")) == source and str(link.get("target")) == target and link.get("relation") == relation for link in links):
        return
    link = {
        "source": source,
        "target": target,
        "value": value,
        "relation": relation,
        "confidence": confidence,
        "weight": 1.0 + (confidence or 0.0),
        "evidence_refs": evidence_refs or [],
        "explanation": explanation,
    }
    if style:
        link["lineStyle"] = style
    links.append(link)


def _resolve_related_causes(related: Any, cause_ref_map: dict[Any, str]) -> list[str]:
    refs = related if isinstance(related, list) else [related]
    result: list[str] = []
    for ref in refs:
        if ref is None:
            continue
        for candidate in (ref, str(ref), str(ref).lower()):
            node_id = cause_ref_map.get(candidate)
            if node_id and node_id not in result:
                result.append(node_id)
                break
    return result


def get_fault_knowledge_graph(fault_code):
    """Return the expanded expert knowledge graph for one fault code."""
    original_code = fault_code
    resolved_code, fault_info = _resolve_fault_knowledge(fault_code)
    if not resolved_code or not fault_info:
        return None

    fault_code = _canonical_fault_code(resolved_code) or resolved_code
    expert = EXPERT_KNOWLEDGE_LIBRARY.get(fault_code, {})
    nodes = [{
        "id": fault_code,
        "name": fault_info["name"],
        "category": 0,
        "symbolSize": 80,
        "value": fault_info["name"],
        "description": fault_info["name"],
        "node_type": "fault",
        "source": "static_kg",
        "confidence": 1.0,
        "risk_level": None,
        "evidence_refs": [],
        "status": "static",
        "keywords": [fault_code, str(fault_info.get("name") or "")],
    }]
    links: list[dict[str, Any]] = []
    cause_ref_map: dict[Any, str] = {}

    core_causes = _expert_items(fault_info.get("root_causes") or [])
    core_solutions = _expert_items(fault_info.get("solutions") or [])
    for index, cause in enumerate(core_causes, start=1):
        cause_id = f"{fault_code}_cause_{index}"
        _add_graph_node(
            nodes,
            node_id=cause_id,
            name=cause["name"],
            category=1,
            node_type="cause",
            value=cause.get("description") or cause["name"],
            symbol_size=62,
            source="static_kg",
            keywords=cause.get("keywords") or [cause["name"]],
        )
        for key in (index, str(index), f"core_{index}", cause["name"], cause["name"].lower()):
            cause_ref_map[key] = cause_id
        _add_graph_link(links, fault_code, cause_id, value="原因", relation="has_cause", explanation=cause.get("description") or "")

    solution_start = len(core_causes) + 1
    for offset, solution in enumerate(core_solutions, start=solution_start):
        solution_id = f"{fault_code}_solution_{offset}"
        _add_graph_node(
            nodes,
            node_id=solution_id,
            name=solution["name"],
            category=2,
            node_type="solution",
            value=solution.get("description") or solution["name"],
            symbol_size=60,
            source="static_kg",
            keywords=solution.get("keywords") or [solution["name"]],
        )
        _add_graph_link(links, fault_code, solution_id, value="解决方案", relation="mitigated_by", explanation=solution.get("description") or "")

    for index, cause in enumerate(_expert_items(expert.get("extra_causes")), start=1):
        cause_id = f"{fault_code}_cause_extra_{index}"
        _add_graph_node(
            nodes,
            node_id=cause_id,
            name=cause["name"],
            category=1,
            node_type="cause",
            value=cause.get("description") or cause["name"],
            symbol_size=58,
            keywords=cause.get("keywords") or [cause["name"]],
        )
        for key in (f"extra_{index}", cause["name"], cause["name"].lower()):
            cause_ref_map[key] = cause_id
        _add_graph_link(links, fault_code, cause_id, value="候选原因", relation="has_candidate_cause", explanation=cause.get("description") or "")

    for index, evidence in enumerate(_expert_items(expert.get("evidence_patterns")), start=1):
        evidence_id = f"{fault_code}_evidence_{index}"
        related = _resolve_related_causes(evidence.get("related_causes"), cause_ref_map)
        _add_graph_node(
            nodes,
            node_id=evidence_id,
            name=evidence["name"],
            category=3,
            node_type="evidence",
            value=evidence.get("description") or evidence["name"],
            symbol_size=42,
            keywords=evidence.get("keywords") or [evidence["name"]],
            related_node_ids=related,
        )
        _add_graph_link(links, fault_code, evidence_id, value="证据模式", relation="has_evidence_pattern", explanation=evidence.get("description") or "")
        for cause_id in related[:4]:
            _add_graph_link(links, evidence_id, cause_id, value="支持", relation="supports_cause", explanation=evidence.get("description") or "")

    for index, check in enumerate(_expert_items(expert.get("checks")), start=1):
        check_id = f"{fault_code}_check_{index}"
        _add_graph_node(
            nodes,
            node_id=check_id,
            name=check["name"],
            category=4,
            node_type="check",
            value=check.get("description") or check["name"],
            symbol_size=44,
            keywords=check.get("keywords") or [check["name"]],
        )
        _add_graph_link(links, fault_code, check_id, value="人工复核", relation="requires_check", explanation=check.get("description") or "")

    for index, action in enumerate(_expert_items(expert.get("actions")), start=1):
        action_id = f"{fault_code}_action_{index}"
        _add_graph_node(
            nodes,
            node_id=action_id,
            name=action["name"],
            category=2,
            node_type="solution",
            value=action.get("description") or action["name"],
            symbol_size=54,
            keywords=action.get("keywords") or [action["name"]],
        )
        _add_graph_link(links, fault_code, action_id, value="建议动作", relation="recommended_action", explanation=action.get("description") or "")

    for index, risk in enumerate(_expert_items(expert.get("risks")), start=1):
        risk_id = f"{fault_code}_risk_{index}"
        _add_graph_node(
            nodes,
            node_id=risk_id,
            name=risk["name"],
            category=6,
            node_type="risk",
            value=risk.get("description") or risk["name"],
            symbol_size=46,
            keywords=risk.get("keywords") or [risk["name"]],
        )
        _add_graph_link(links, fault_code, risk_id, value="风险", relation="has_risk", explanation=risk.get("description") or "")

    return {
        "schema_version": REASONING_GRAPH_SCHEMA_VERSION,
        "fault_code": fault_code,
        "original_fault_code": original_code,
        "nodes": nodes,
        "links": links,
        "categories": GRAPH_CATEGORIES,
        "reasoning": {
            "mode": "static",
            "status": "static",
            "summary": "本地专家知识库图谱，尚未叠加工单级 DeepSeek 推理结果。",
        },
        "evidence": [],
    }


def _safe_float(value: Any) -> float | None:
    try:
        number = float(value)
    except (TypeError, ValueError):
        return None
    return max(0.0, min(1.0, number))


def _safe_result_dict(task: Any = None, result: dict[str, Any] | None = None) -> dict[str, Any]:
    if isinstance(result, dict):
        return result
    raw = getattr(task, "result_json", None)
    if not raw:
        return {}
    try:
        parsed = json.loads(raw)
    except Exception:
        return {}
    return parsed if isinstance(parsed, dict) else {}


def _text(value: Any, limit: int = 500) -> str:
    if value is None:
        return ""
    if isinstance(value, dict):
        for key in ("text", "name", "summary", "description", "explanation", "value"):
            if value.get(key):
                return str(value.get(key))[:limit]
        return json.dumps(value, ensure_ascii=False, sort_keys=True)[:limit]
    return str(value)[:limit]


def _items(value: Any) -> list[Any]:
    if value is None:
        return []
    if isinstance(value, list):
        return [item for item in value if _text(item).strip()]
    return [value] if _text(value).strip() else []


def _refs(value: Any) -> list[str]:
    values = value if isinstance(value, list) else [value]
    result = []
    for item in values:
        if item is None:
            continue
        text = str(item).strip()
        if text:
            result.append(text[:120])
    return result


def _merge_unique(current: list[Any] | None, incoming: list[Any]) -> list[str]:
    seen = {str(item) for item in (current or [])}
    merged = [str(item) for item in (current or [])]
    for item in incoming:
        value = str(item)
        if value not in seen:
            seen.add(value)
            merged.append(value)
    return merged


def _node_index(nodes: list[dict[str, Any]]) -> dict[str, str]:
    index: dict[str, str] = {}
    for node in nodes:
        node_id = str(node.get("id") or "")
        name = str(node.get("name") or "")
        if node_id:
            index[node_id.lower()] = node_id
        if name:
            index[name.lower()] = node_id
    return index


def _match_node_refs(refs: list[str], nodes: list[dict[str, Any]], index: dict[str, str]) -> list[str]:
    matched: list[str] = []
    matched_set: set[str] = set()
    for ref in refs:
        lowered = str(ref or "").strip().lower()
        if not lowered:
            continue
        direct = index.get(lowered)
        if direct and direct not in matched_set:
            matched_set.add(direct)
            matched.append(direct)
            continue
        suffix = ""
        for marker in ("_cause_", "_solution_"):
            if marker in lowered:
                suffix = marker + lowered.rsplit(marker, 1)[1]
                break
        if suffix:
            suffix_matched = False
            for node in nodes:
                node_id = str(node.get("id") or "")
                if node_id.lower().endswith(suffix) and node_id not in matched_set:
                    matched_set.add(node_id)
                    matched.append(node_id)
                    suffix_matched = True
                    break
            if suffix_matched:
                continue
        for node in nodes:
            node_id = str(node.get("id") or "")
            haystack = " ".join([
                str(node.get("id") or ""),
                str(node.get("name") or ""),
                str(node.get("value") or ""),
            ]).lower()
            if len(lowered) >= 4 and (lowered in haystack or haystack in lowered):
                if node_id and node_id not in matched_set:
                    matched_set.add(node_id)
                    matched.append(node_id)
                break
    return matched


def _match_nodes_by_text(text: str, nodes: list[dict[str, Any]]) -> list[str]:
    lowered = str(text or "").lower()
    if not lowered:
        return []
    matched: list[str] = []
    for node in nodes:
        node_id = str(node.get("id") or "")
        name = str(node.get("name") or "").lower()
        value = str(node.get("value") or "").lower()
        if name and len(name) >= 4 and name in lowered:
            matched.append(node_id)
        elif value and len(value) >= 10 and value[:60] in lowered:
            matched.append(node_id)
    return matched[:4]


def _mark_node(nodes_by_id: dict[str, dict[str, Any]], node_id: str, refs: list[str], confidence: float | None, risk_level: str | None) -> None:
    node = nodes_by_id.get(node_id)
    if not node:
        return
    node["source"] = "static_kg+deepseek"
    node["status"] = "matched"
    node["evidence_refs"] = _merge_unique(node.get("evidence_refs"), refs)
    if confidence is not None:
        current = _safe_float(node.get("confidence"))
        node["confidence"] = max(current or 0.0, confidence)
    if risk_level:
        node["risk_level"] = risk_level
    style = dict(node.get("itemStyle") or {})
    style.update({"borderColor": "#2563eb", "borderWidth": 2, "shadowBlur": 10, "shadowColor": "rgba(37,99,235,0.25)"})
    node["itemStyle"] = style


def _upsert_link(links: list[dict[str, Any]], source: str, target: str, *, relation: str, value: str,
                 confidence: float | None, refs: list[str], explanation: str) -> None:
    for link in links:
        if str(link.get("source")) == source and str(link.get("target")) == target:
            link["relation"] = relation or link.get("relation")
            link["value"] = value or link.get("value")
            link["evidence_refs"] = _merge_unique(link.get("evidence_refs"), refs)
            if confidence is not None:
                current = _safe_float(link.get("confidence"))
                link["confidence"] = max(current or 0.0, confidence)
                link["weight"] = max(float(link.get("weight") or 1.0), 1.0 + confidence)
            if explanation:
                link["explanation"] = explanation
            style = dict(link.get("lineStyle") or {})
            style.update({"width": 4, "color": "#2563eb"})
            link["lineStyle"] = style
            return
    links.append({
        "source": source,
        "target": target,
        "value": value,
        "relation": relation,
        "confidence": confidence,
        "weight": 1.0 + (confidence or 0.0),
        "evidence_refs": refs,
        "explanation": explanation,
        "lineStyle": {
            "width": 3,
            "color": "#2563eb",
            "type": "dashed",
            "curveness": 0.18,
        },
    })


def _node_name(nodes_by_id: dict[str, dict[str, Any]], node_id: str) -> str:
    node = nodes_by_id.get(node_id) or {}
    return str(node.get("name") or node_id)


def _add_reasoning_path(
    links: list[dict[str, Any]],
    paths: list[dict[str, Any]],
    nodes_by_id: dict[str, dict[str, Any]],
    source: str,
    target: str,
    *,
    confidence: float | None,
    refs: list[str],
    explanation: str,
) -> None:
    if not source or not target or source == target:
        return
    path = {
        "source": source,
        "target": target,
        "source_name": _node_name(nodes_by_id, source),
        "target_name": _node_name(nodes_by_id, target),
        "confidence": confidence,
        "evidence_refs": refs,
        "explanation": explanation,
    }
    key = (source, target)
    if not any((item.get("source"), item.get("target")) == key for item in paths):
        paths.append(path)
    _upsert_link(
        links,
        source,
        target,
        relation="ai_reasoning_path",
        value="AI推理路径",
        confidence=confidence,
        refs=refs,
        explanation=explanation,
    )


def _clone_graph_payload(graph: dict[str, Any]) -> dict[str, Any]:
    return json.loads(json.dumps(graph, ensure_ascii=False, default=str))


def _event_signal_tokens_for_snapshot(snap: Any) -> list[str]:
    label = str(getattr(snap, "channel_label", "") or "")
    channel_id = getattr(snap, "channel_id", None)
    tokens = [label, str(getattr(snap, "snapshot_type", "") or ""), f"channel_{channel_id}"]
    if "+" in label:
        tokens.extend(["voltage_pos", "直流母线(+)", "voltage", "range"])
    if "-" in label:
        tokens.extend(["voltage_neg", "直流母线(-)", "voltage", "range"])
    if "漏" in label:
        tokens.extend(["leakage", "漏电流", "ground"])
    if "负载" in label or "电流" in label:
        tokens.extend(["current", "负载电流"])
    std = _safe_float(getattr(snap, "std_value", None)) or 0.0
    max_value = _safe_float(getattr(snap, "max_value", None)) or 0.0
    min_value = _safe_float(getattr(snap, "min_value", None)) or 0.0
    if std > 1:
        tokens.extend(["std", "ripple"])
    if abs(max_value - min_value) > 10:
        tokens.extend(["range", "max", "min"])
    return tokens


def _load_work_order_event_profile(work_order_id: int | None, fault_code: str) -> dict[str, Any]:
    if not work_order_id:
        return {"signal_text": fault_code, "evidence_nodes": [], "evidence_hash": "no_work_order", "snapshots": 0, "history_points": 0}
    try:
        from edgewind.models import FaultSnapshot, HistoryData, WorkOrder
    except Exception:
        return {"signal_text": fault_code, "evidence_nodes": [], "evidence_hash": "models_unavailable", "snapshots": 0, "history_points": 0}

    try:
        order = WorkOrder.query.get(work_order_id)
        if not order:
            return {"signal_text": fault_code, "evidence_nodes": [], "evidence_hash": "order_missing", "snapshots": 0, "history_points": 0}
        if order.fault_time:
            start = order.fault_time - timedelta(minutes=10)
            end = order.fault_time + timedelta(minutes=10)
        else:
            end = datetime.utcnow()
            start = end - timedelta(minutes=20)

        snapshots = FaultSnapshot.query.filter(
            FaultSnapshot.device_id == order.device_id,
            FaultSnapshot.fault_code == fault_code,
            FaultSnapshot.timestamp >= start,
            FaultSnapshot.timestamp <= end,
        ).order_by(FaultSnapshot.timestamp.desc()).limit(32).all()

        history = HistoryData.query.filter(
            HistoryData.device_id == order.device_id,
            HistoryData.timestamp >= (start - timedelta(minutes=50)),
            HistoryData.timestamp <= end,
        ).order_by(HistoryData.timestamp.asc()).limit(1200).all()
    except Exception:
        return {"signal_text": fault_code, "evidence_nodes": [], "evidence_hash": "db_unavailable", "snapshots": 0, "history_points": 0}

    signal_tokens = [fault_code, str(getattr(order, "fault_type", "") or ""), str(getattr(order, "status", "") or "")]
    evidence_rows = []
    for snap in snapshots:
        signal_tokens.extend(_event_signal_tokens_for_snapshot(snap))
        label = str(getattr(snap, "channel_label", "") or f"CH{getattr(snap, 'channel_id', '')}")
        current = _safe_float(getattr(snap, "current_value", None))
        std = _safe_float(getattr(snap, "std_value", None)) or 0.0
        max_value = _safe_float(getattr(snap, "max_value", None)) or 0.0
        min_value = _safe_float(getattr(snap, "min_value", None)) or 0.0
        spread = abs(max_value - min_value)
        score = std + spread * 0.01 + (abs(current or 0.0) * 0.001)
        evidence_rows.append({
            "score": score,
            "summary": f"{getattr(snap, 'snapshot_type', '')} {label}: current={current}, std={round(std, 3)}, range={round(spread, 3)}",
            "tokens": _event_signal_tokens_for_snapshot(snap),
        })

    history_fields = [
        "voltage_pos",
        "voltage_neg",
        "current",
        "leakage",
        "t_igbt_c",
        "t_dc_cap_c",
        "rh_cabinet_pct",
        "wind_load_pct",
    ]
    for field in history_fields:
        values = []
        for row in history:
            value = _safe_float(getattr(row, field, None))
            if value is not None:
                values.append(value)
        if not values:
            continue
        delta = values[-1] - values[0]
        std = pstdev(values) if len(values) > 1 else 0.0
        signal_tokens.extend([field, "history", "delta" if abs(delta) > 1 else "", "std" if std > 1 else ""])
        evidence_rows.append({
            "score": abs(delta) + std * 0.2,
            "summary": f"history {field}: first={round(values[0], 3)}, last={round(values[-1], 3)}, delta={round(delta, 3)}, std={round(std, 3)}",
            "tokens": [field, "history", "delta", "std"],
        })

    evidence_rows.sort(key=lambda item: item["score"], reverse=True)
    evidence_nodes = []
    for index, item in enumerate(evidence_rows[:4], start=1):
        evidence_nodes.append({
            "id": f"{fault_code}_event_evidence_{work_order_id}_{index}",
            "name": f"本次证据 {index}",
            "category": 3,
            "symbolSize": 44,
            "value": item["summary"],
            "description": item["summary"],
            "node_type": "evidence",
            "source": "event_snapshot",
            "confidence": min(0.95, 0.45 + min(item["score"], 100.0) / 200.0),
            "risk_level": None,
            "evidence_refs": [f"event_{index}"],
            "status": "matched",
            "keywords": item["tokens"],
        })

    material = {
        "work_order_id": work_order_id,
        "fault_code": fault_code,
        "signals": [token for token in signal_tokens if token],
        "evidence": [node["value"] for node in evidence_nodes],
        "snapshots": len(snapshots),
        "history_points": len(history),
    }
    evidence_hash = hashlib.sha256(json.dumps(material, ensure_ascii=False, sort_keys=True, default=str).encode("utf-8")).hexdigest()[:12]
    return {
        "signal_text": " ".join(str(token).lower() for token in signal_tokens if token),
        "evidence_nodes": evidence_nodes,
        "evidence_hash": evidence_hash,
        "snapshots": len(snapshots),
        "history_points": len(history),
    }


def _node_event_score(node: dict[str, Any], signal_text: str, evidence_hash: str, forced_ids: set[str]) -> float:
    node_id = str(node.get("id") or "")
    if node.get("node_type") == "fault":
        return 999.0
    score = 0.0
    if node_id in forced_ids or node.get("status") in ("matched", "event_matched"):
        score += 50.0
    for keyword in node.get("keywords") or []:
        value = str(keyword or "").strip().lower()
        if value and value in signal_text:
            score += 4.0
    for text in (node.get("name"), node.get("value"), node.get("description")):
        value = str(text or "").strip().lower()
        if value and len(value) >= 4 and value in signal_text:
            score += 2.0
    # Keep the deterministic tie-breaker tiny; otherwise unrelated nodes enter
    # the event graph just because their hash is high.
    tie = int(hashlib.sha256(f"{evidence_hash}|{node_id}".encode("utf-8")).hexdigest()[:4], 16) / 65535.0 * 0.2
    return score + tie


def _attach_graph_views(default_graph: dict[str, Any], full_graph: dict[str, Any], *, view: str, profile: dict[str, Any]) -> dict[str, Any]:
    default_graph["graph_view"] = view
    default_graph["event_profile"] = {
        "evidence_hash": profile.get("evidence_hash"),
        "snapshots": profile.get("snapshots", 0),
        "history_points": profile.get("history_points", 0),
    }
    default_graph["graph_views"] = {
        "event": {
            "nodes": default_graph.get("nodes", []),
            "links": default_graph.get("links", []),
            "categories": default_graph.get("categories", GRAPH_CATEGORIES),
            "label": "本次推理",
        },
        "full": {
            "nodes": full_graph.get("nodes", []),
            "links": full_graph.get("links", []),
            "categories": full_graph.get("categories", GRAPH_CATEGORIES),
            "label": "全部知识库",
        },
    }
    return default_graph


def _build_event_subgraph(full_graph: dict[str, Any], *, work_order_id: int | None,
                          evidence_items: list[dict[str, Any]] | None = None,
                          reasoning_paths: list[dict[str, Any]] | None = None) -> dict[str, Any]:
    if not work_order_id and not evidence_items and not reasoning_paths:
        return full_graph

    graph = _clone_graph_payload(full_graph)
    full_graph = _clone_graph_payload(full_graph)
    fault_code = str(graph.get("fault_code") or "")
    profile = _load_work_order_event_profile(work_order_id, fault_code)
    nodes = graph.get("nodes", [])
    links = graph.get("links", [])
    nodes_by_id = {str(node.get("id")): node for node in nodes}

    forced_ids = {fault_code}
    for item in evidence_items or []:
        forced_ids.update(str(node_id) for node_id in (item.get("node_ids") or []))
    for path in reasoning_paths or []:
        forced_ids.add(str(path.get("source") or ""))
        forced_ids.add(str(path.get("target") or ""))

    event_nodes = profile.get("evidence_nodes") or []
    for event_node in event_nodes:
        nodes_by_id[event_node["id"]] = event_node
        nodes.append(event_node)

    scored = sorted(
        ((node, _node_event_score(node, profile.get("signal_text", ""), profile.get("evidence_hash", ""), forced_ids)) for node in nodes),
        key=lambda item: item[1],
        reverse=True,
    )
    selected: set[str] = {fault_code}
    limits = {"cause": 3, "solution": 3, "evidence": 3, "check": 2, "risk": 1}
    counts = {key: 0 for key in limits}
    for node, score in scored:
        node_id = str(node.get("id") or "")
        node_type = str(node.get("node_type") or "")
        bucket = "evidence" if node_type in ("evidence", "parameter") else node_type
        if node_id in forced_ids:
            selected.add(node_id)
            continue
        if bucket in limits and counts[bucket] < limits[bucket] and score > 1.2:
            selected.add(node_id)
            counts[bucket] += 1

    selected_causes = [node_id for node_id in selected if nodes_by_id.get(node_id, {}).get("node_type") == "cause"]
    selected_actions = [node_id for node_id in selected if nodes_by_id.get(node_id, {}).get("node_type") == "solution"]
    if not selected_causes:
        selected_causes = [node.get("id") for node, _score in scored if node.get("node_type") == "cause"][:3]
        selected.update(str(node_id) for node_id in selected_causes if node_id)
    if not selected_actions:
        selected_actions = [node.get("id") for node, _score in scored if node.get("node_type") == "solution"][:3]
        selected.update(str(node_id) for node_id in selected_actions if node_id)

    for event_node in event_nodes:
        selected.add(event_node["id"])
        for cause_id in selected_causes[:1]:
            _add_graph_link(
                links,
                event_node["id"],
                str(cause_id),
                value="本次证据支持",
                relation="event_supports_cause",
                explanation=event_node.get("value", ""),
                confidence=event_node.get("confidence"),
                evidence_refs=event_node.get("evidence_refs") or [],
                style={"width": 3, "color": "#2563eb", "type": "solid", "curveness": 0.18},
            )

    for cause_id in selected_causes[:3]:
        for action_id in selected_actions[:1]:
            _add_graph_link(
                links,
                str(cause_id),
                str(action_id),
                value="建议处置",
                relation="event_suggests_action",
                explanation=f"{_node_name(nodes_by_id, str(cause_id))} -> {_node_name(nodes_by_id, str(action_id))}",
                confidence=0.62,
                style={"width": 2, "color": "#60a5fa", "type": "dashed", "curveness": 0.22},
            )

    event_links = [
        link for link in links
        if str(link.get("source")) in selected and str(link.get("target")) in selected
    ]
    event_nodes_selected = [node for node in nodes if str(node.get("id")) in selected]
    for node in event_nodes_selected:
        if str(node.get("id")) in selected and node.get("node_type") != "fault":
            node["status"] = "event_matched" if node.get("status") == "static" else node.get("status")

    graph["nodes"] = event_nodes_selected
    graph["links"] = event_links
    graph["categories"] = GRAPH_CATEGORIES
    return _attach_graph_views(graph, full_graph, view="event", profile=profile)


def build_reasoning_graph(
    fault_code: str,
    *,
    task: Any = None,
    result: dict[str, Any] | None = None,
    work_order_id: int | None = None,
) -> dict[str, Any] | None:
    """Return a static ECharts graph with optional cached DeepSeek reasoning overlay."""
    graph = get_fault_knowledge_graph(fault_code)
    if not graph:
        return None

    graph["categories"] = GRAPH_CATEGORIES
    reasoning = dict(graph.get("reasoning") or {})
    if work_order_id is not None:
        reasoning["work_order_id"] = work_order_id

    status = getattr(task, "status", None)
    task_id = getattr(task, "task_id", None)
    if task_id:
        reasoning["task_id"] = task_id
    if status:
        reasoning["task_status"] = status

    if task is not None and status in ("queued", "running"):
        reasoning.update({"mode": status, "status": status, "summary": "DeepSeek 图谱推理任务正在后台处理。"})
        graph["reasoning"] = reasoning
        return _build_event_subgraph(graph, work_order_id=work_order_id)
    if task is not None and status == "failed":
        reasoning.update({
            "mode": "failed",
            "status": "failed",
            "summary": "DeepSeek 图谱推理失败，当前显示本地静态知识图谱。",
            "error": getattr(task, "error_message", None),
        })
        graph["reasoning"] = reasoning
        return _build_event_subgraph(graph, work_order_id=work_order_id)

    result_data = _safe_result_dict(task, result)
    if not result_data:
        graph["reasoning"] = reasoning
        return _build_event_subgraph(graph, work_order_id=work_order_id)

    nodes = graph["nodes"]
    links = graph["links"]
    nodes_by_id = {str(node.get("id")): node for node in nodes}
    index = _node_index(nodes)
    confidence = _safe_float(result_data.get("confidence"))
    risk_level = str(result_data.get("risk_level") or "")[:80] or None
    reasoning_paths: list[dict[str, Any]] = []

    evidence_items = []
    for idx, item in enumerate(_items(result_data.get("evidence")), start=1):
        if isinstance(item, dict):
            ev_id = str(item.get("id") or item.get("evidence_id") or f"ev_{idx}")
            text = _text(item.get("text") or item.get("description") or item.get("summary") or item)
            source_refs = (
                _refs(item.get("source_node_ids")) or
                _refs(item.get("node_ids")) or
                _refs(item.get("mapped_node_ids")) or
                _refs(item.get("target_node_ids")) or
                _refs(item.get("source_node_id")) or
                _refs(item.get("node_id"))
            )
            ev_confidence = _safe_float(item.get("confidence")) or confidence
        else:
            ev_id = f"ev_{idx}"
            text = _text(item)
            source_refs = []
            ev_confidence = confidence

        matched_nodes = _match_node_refs(source_refs, nodes, index) if source_refs else []
        if not matched_nodes:
            matched_nodes = _match_nodes_by_text(text, nodes)

        evidence_items.append({
            "id": ev_id,
            "text": text,
            "confidence": ev_confidence,
            "node_ids": matched_nodes,
            "status": "matched" if matched_nodes else "unmatched",
        })

        for node_id in matched_nodes:
            _mark_node(nodes_by_id, node_id, [ev_id], ev_confidence, risk_level)

    ranked_causes = []
    for item in _items(result_data.get("ranked_causes") or result_data.get("root_causes")):
        item_text = _text(item)
        item_confidence = _safe_float(item.get("confidence")) if isinstance(item, dict) else confidence
        refs = _refs(item.get("evidence_refs")) if isinstance(item, dict) else []
        node_refs = _refs(item.get("node_id") or item.get("id") or item.get("name")) if isinstance(item, dict) else [item_text]
        matched_nodes = _match_node_refs(node_refs, nodes, index) or _match_nodes_by_text(item_text, nodes)
        if matched_nodes and item_text.strip().startswith("{"):
            item_text = _node_name(nodes_by_id, matched_nodes[0])
        for node_id in matched_nodes:
            _mark_node(nodes_by_id, node_id, refs, item_confidence, risk_level)
        ranked_causes.append({"text": item_text, "confidence": item_confidence, "node_ids": matched_nodes, "evidence_refs": refs})

    recommended_actions = []
    for item in _items(result_data.get("recommended_actions") or result_data.get("actions")):
        item_text = _text(item)
        item_confidence = _safe_float(item.get("confidence")) if isinstance(item, dict) else confidence
        refs = _refs(item.get("evidence_refs")) if isinstance(item, dict) else []
        node_refs = _refs(item.get("node_id") or item.get("id") or item.get("name")) if isinstance(item, dict) else [item_text]
        matched_nodes = _match_node_refs(node_refs, nodes, index) or _match_nodes_by_text(item_text, nodes)
        if matched_nodes and item_text.strip().startswith("{"):
            item_text = _node_name(nodes_by_id, matched_nodes[0])
        for node_id in matched_nodes:
            _mark_node(nodes_by_id, node_id, refs, item_confidence, risk_level)
        recommended_actions.append({"text": item_text, "confidence": item_confidence, "node_ids": matched_nodes, "evidence_refs": refs})

    cause_node_ids = [
        node_id
        for item in ranked_causes
        for node_id in item.get("node_ids", [])
        if nodes_by_id.get(node_id, {}).get("node_type") == "cause"
    ]
    if not cause_node_ids:
        cause_node_ids = [
            node_id
            for evidence in evidence_items
            for node_id in evidence.get("node_ids", [])
            if nodes_by_id.get(node_id, {}).get("node_type") == "cause"
        ]
    action_node_ids = [
        node_id
        for item in recommended_actions
        for node_id in item.get("node_ids", [])
        if nodes_by_id.get(node_id, {}).get("node_type") == "solution"
    ]
    if cause_node_ids and action_node_ids:
        seen_paths: set[tuple[str, str]] = set()
        for cause_id in cause_node_ids[:3]:
            cause_refs = []
            cause_confidence = confidence
            cause_text = ""
            for cause in ranked_causes:
                if cause_id in cause.get("node_ids", []):
                    cause_refs = _merge_unique(cause_refs, cause.get("evidence_refs") or [])
                    cause_confidence = cause.get("confidence") if cause.get("confidence") is not None else cause_confidence
                    cause_text = cause.get("text") or cause_text
            for action_id in action_node_ids[:3]:
                key = (cause_id, action_id)
                if key in seen_paths:
                    continue
                seen_paths.add(key)
                action_refs = []
                action_text = ""
                action_confidence = confidence
                for action in recommended_actions:
                    if action_id in action.get("node_ids", []):
                        action_refs = _merge_unique(action_refs, action.get("evidence_refs") or [])
                        action_confidence = action.get("confidence") if action.get("confidence") is not None else action_confidence
                        action_text = action.get("text") or action_text
                refs = _merge_unique(cause_refs, action_refs)
                for evidence in evidence_items:
                    if cause_id in evidence.get("node_ids", []) or action_id in evidence.get("node_ids", []):
                        if evidence.get("id"):
                            refs = _merge_unique(refs, [evidence.get("id")])
                path_confidence = cause_confidence if cause_confidence is not None else action_confidence
                explanation = f"{_node_name(nodes_by_id, cause_id)} -> {_node_name(nodes_by_id, action_id)}"
                _add_reasoning_path(
                    links,
                    reasoning_paths,
                    nodes_by_id,
                    cause_id,
                    action_id,
                    confidence=path_confidence,
                    refs=refs,
                    explanation=explanation,
                )
                if len(reasoning_paths) >= 4:
                    break
            if len(reasoning_paths) >= 4:
                break

    for edge in _items(result_data.get("graph_edges")):
        if not isinstance(edge, dict):
            continue
        source_nodes = _match_node_refs(_refs(edge.get("source")), nodes, index)
        target_nodes = _match_node_refs(_refs(edge.get("target")), nodes, index)
        if not source_nodes or not target_nodes:
            continue
        edge_refs = _refs(edge.get("evidence_refs"))
        edge_confidence = _safe_float(edge.get("confidence")) or confidence
        _add_reasoning_path(
            links,
            reasoning_paths,
            nodes_by_id,
            source_nodes[0],
            target_nodes[0],
            confidence=edge_confidence,
            refs=edge_refs,
            explanation=_text(edge.get("explanation"), 800),
        )

    evidence_by_id = {
        str(item.get("id")): item.get("text")
        for item in evidence_items
        if item.get("id") and item.get("text")
    }
    for index, evidence in enumerate(evidence_items, start=1):
        evidence_node_id = f"{fault_code}_ai_evidence_{index}"
        if evidence_node_id not in nodes_by_id:
            node = {
                "id": evidence_node_id,
                "name": f"AI证据 {index}",
                "category": 3,
                "symbolSize": 46,
                "value": evidence.get("text") or "",
                "description": evidence.get("text") or "",
                "node_type": "evidence",
                "source": "deepseek",
                "confidence": evidence.get("confidence"),
                "risk_level": risk_level,
                "evidence_refs": [str(evidence.get("id"))] if evidence.get("id") else [],
                "status": evidence.get("status") or "matched",
                "keywords": [str(evidence.get("id") or ""), "deepseek", "evidence"],
            }
            nodes.append(node)
            nodes_by_id[evidence_node_id] = node
        for node_id in evidence.get("node_ids", [])[:4]:
            _add_graph_link(
                links,
                evidence_node_id,
                str(node_id),
                value="证据支持",
                relation="ai_evidence_supports",
                explanation=evidence.get("text") or "",
                confidence=evidence.get("confidence"),
                evidence_refs=[str(evidence.get("id"))] if evidence.get("id") else [],
                style={"width": 3, "color": "#2563eb", "type": "solid", "curveness": 0.18},
            )
    for path in reasoning_paths:
        refs = [str(ref) for ref in (path.get("evidence_refs") or [])]
        path["supporting_evidence"] = [
            evidence_by_id[ref]
            for ref in refs
            if ref in evidence_by_id
        ]

    reasoning.update({
        "mode": "hybrid",
        "status": "succeeded",
        "summary": _text(result_data.get("summary"), 1200),
        "risk_level": risk_level,
        "confidence": confidence,
        "ranked_causes": ranked_causes,
        "recommended_actions": recommended_actions,
        "paths": reasoning_paths,
        "human_review": [_text(item) for item in _items(result_data.get("human_review"))],
        "data_gaps": [_text(item) for item in _items(result_data.get("data_gaps"))],
        "evidence_manifest": result_data.get("evidence_manifest") or {},
    })
    graph["reasoning"] = reasoning
    graph["evidence"] = evidence_items
    graph["ai_overlay"] = {
        "matched_node_ids": sorted({
            node_id
            for evidence in evidence_items
            for node_id in evidence.get("node_ids", [])
        }),
        "reasoning_paths": reasoning_paths,
    }
    return _build_event_subgraph(graph, work_order_id=work_order_id, evidence_items=evidence_items, reasoning_paths=reasoning_paths)
