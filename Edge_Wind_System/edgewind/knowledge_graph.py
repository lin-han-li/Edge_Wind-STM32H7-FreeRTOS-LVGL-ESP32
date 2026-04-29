"""
故障诊断知识图谱模块
定义故障代码、知识图谱数据和相关处理函数
"""
from datetime import datetime

# ==================== 故障代码映射 ====================
# E00=Normal, E01=AC Intrusion, E02=Insulation Fault, E03=DC Capacitor Aging, 
# E04=IGBT Open Circuit, E05=DC Bus Grounding

FAULT_CODE_MAP = {
    "E00": None,  # Normal - 无故障
    "E01": "AC_INTRUSION",
    "E02": "INSULATION_FAULT",
    "E03": "DC_CAPACITOR_AGING",
    "E04": "IGBT_OPEN_CIRCUIT",
    "E05": "DC_BUS_GROUNDING"
}

# ==================== 故障诊断知识图谱 ====================

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


def get_fault_knowledge_graph(fault_code):
    """
    根据故障代码获取知识图谱数据（ECharts力导向图格式）
    
    Args:
        fault_code: 故障代码 (E01, E02) 或故障类型名称 (AC_INTRUSION, INSULATION_FAULT)
        
    Returns:
        dict: 包含nodes, links, categories的知识图谱数据，如果无效则返回None
    """
    original_code = fault_code
    
    # 如果是E00-E05格式，先尝试直接查找（支持新格式）
    if fault_code in FAULT_KNOWLEDGE_GRAPH:
        fault_info = FAULT_KNOWLEDGE_GRAPH[fault_code]
        # 如果找到但缺少root_causes字段，尝试通过映射转换
        if "root_causes" not in fault_info:
            mapped_code = FAULT_CODE_MAP.get(fault_code)
            if mapped_code and mapped_code in FAULT_KNOWLEDGE_GRAPH:
                fault_info = FAULT_KNOWLEDGE_GRAPH[mapped_code]
                fault_code = mapped_code
            else:
                # 如果既没有root_causes也没有映射，返回None
                return None
    else:
        # 尝试通过FAULT_CODE_MAP转换
        if fault_code in FAULT_CODE_MAP:
            if FAULT_CODE_MAP[fault_code] is None:
                return None  # E00 = Normal，无故障
            fault_code = FAULT_CODE_MAP[fault_code]
        
        if fault_code not in FAULT_KNOWLEDGE_GRAPH:
            return None
        
        fault_info = FAULT_KNOWLEDGE_GRAPH[fault_code]
    
    # 构建节点
    nodes = [
        {
            "id": fault_code,
            "name": fault_info["name"],
            "category": 0,  # 0=故障（红色）
            "symbolSize": 80,
            "value": fault_info["name"]
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
            "value": cause["description"]
        })
        links.append({
            "source": fault_code,
            "target": cause_id,
            "value": "原因"
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
            "value": solution["description"]
        })
        links.append({
            "source": fault_code,
            "target": solution_id,
            "value": "解决方案"
        })
        node_id_counter += 1
    
    return {
        "nodes": nodes,
        "links": links,
        "categories": [
            {"name": "故障"},
            {"name": "根本原因"},
            {"name": "解决方案"}
        ]
    }

