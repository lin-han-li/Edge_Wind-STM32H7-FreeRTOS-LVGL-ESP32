"""
工具函数和快照处理模块
包含数据缓冲、快照保存等功能
"""
from collections import deque
from datetime import datetime
import numpy as np
import json
import logging

# ==================== 数据缓冲区 ====================
# 每个节点保存最近10次的正常数据（用于故障发生时获取故障前数据）
node_data_buffer = {}  # {device_id: deque of normal data}
node_fault_data_buffer = {}  # {device_id: last fault data}
BUFFER_SIZE = 10  # 保存最近10次数据

# 记录每个节点的上一次故障状态（用于检测状态变化）
node_fault_states = {}  # {device_id: 'E00' or 'E01', etc.}

# 记录每个节点的快照保存状态（避免重复保存）
node_snapshot_saved = {}  # {device_id: {'fault_code': 'E01', 'saved_types': ['before', 'after']}}

logger = logging.getLogger(__name__)


def save_to_buffer(device_id, data, is_fault=False):
    """保存数据到缓冲区"""
    if is_fault:
        # 保存故障数据（只保存最新一次）
        node_fault_data_buffer[device_id] = {
            'timestamp': datetime.utcnow(),
            'data': data
        }
    else:
        # 保存正常数据（保存最近10次）
        if device_id not in node_data_buffer:
            node_data_buffer[device_id] = deque(maxlen=BUFFER_SIZE)
        node_data_buffer[device_id].append({
            'timestamp': datetime.utcnow(),
            'data': data
        })


def get_latest_normal_data(device_id):
    """获取最近一次的正常数据"""
    if device_id in node_data_buffer and len(node_data_buffer[device_id]) > 0:
        return node_data_buffer[device_id][-1]  # 返回最新的数据
    return None


def get_latest_fault_data(device_id):
    """获取最近一次的故障数据"""
    return node_fault_data_buffer.get(device_id, None)


def calculate_statistics(waveform):
    """计算波形统计信息"""
    if not waveform or len(waveform) == 0:
        return {'mean': 0, 'std': 0, 'max': 0, 'min': 0}
    
    arr = np.array(waveform)
    return {
        'mean': float(np.mean(arr)),
        'std': float(np.std(arr)),
        'max': float(np.max(arr)),
        'min': float(np.min(arr))
    }


def save_fault_snapshot(db, app, device_id, fault_code, snapshot_type, data):
    """
    保存故障快照到数据库（在后台线程中执行）
    
    Args:
        db: SQLAlchemy数据库实例
        app: Flask应用实例
        device_id: 设备ID
        fault_code: 故障代码 (E01-E05)
        snapshot_type: 'before' 或 'after'
        data: 完整的数据包（包含channels数组）
    """
    # 导入模型（延迟导入避免循环依赖）
    from edgewind.models import FaultSnapshot
    
    # 关键：在后台线程中必须使用Flask应用上下文
    with app.app_context():
        try:
            if not data or 'channels' not in data:
                logger.warning(f"数据格式不正确，无法保存快照: {device_id}")
                return
            
            # 为每个通道保存一条快照记录
            for channel in data['channels']:
                # 计算统计信息
                waveform = channel.get('waveform', [])
                stats = calculate_statistics(waveform)
                
                # 获取当前值（兼容value和current_value两种字段名）
                current_val = channel.get('current_value') or channel.get('value', 0)
                if current_val is None:
                    current_val = 0
                
                snapshot = FaultSnapshot(
                    device_id=device_id,
                    fault_code=fault_code,
                    snapshot_type=snapshot_type,
                    timestamp=datetime.utcnow(),
                    channel_id=channel.get('id', 0),
                    channel_label=channel.get('label', ''),
                    channel_type=channel.get('type', ''),
                    current_value=float(current_val),
                    waveform_data=json.dumps(waveform),
                    fft_data=json.dumps(channel.get('fft_spectrum', [])),
                    mean_value=stats['mean'],
                    std_value=stats['std'],
                    max_value=stats['max'],
                    min_value=stats['min']
                )
                db.session.add(snapshot)
            
            db.session.commit()
            logger.info(f"✅ 已保存故障快照: {device_id} - {fault_code} - {snapshot_type} (共{len(data['channels'])}个通道)")
        except Exception as e:
            db.session.rollback()
            logger.error(f"❌ 保存故障快照失败: {device_id} - {fault_code} - {snapshot_type}, 错误: {e}")
            import traceback
            logger.error(traceback.format_exc())


def create_work_order_from_fault(db, device_id, fault_code, location, fault_time=None):
    """
    从故障自动创建工单
    
    Args:
        db: SQLAlchemy数据库实例
        device_id: 设备ID
        fault_code: 故障代码（知识图谱键）
        location: 设备位置
        fault_time: 故障时间（如果为None则使用当前时间）
    
    Returns:
        WorkOrder对象，如果创建失败则返回None
    """
    from edgewind.models import Device, WorkOrder
    from edgewind.knowledge_graph import FAULT_KNOWLEDGE_GRAPH, FAULT_CODE_MAP, generate_ai_report
    
    try:
        # 安全检查：确保所有参数有效
        if not device_id:
            logger.warning(f"警告: device_id 为空")
            return None
        
        if not fault_code or fault_code not in FAULT_KNOWLEDGE_GRAPH:
            logger.warning(f"警告: 故障代码 {fault_code} 不在知识图谱中")
            return None
        
        # 确保设备存在于数据库中（防止外键约束错误）
        device = Device.query.filter_by(device_id=device_id).first()
        if not device:
            # 自动创建设备记录（原子性操作）
            try:
                device = Device(
                    device_id=device_id,
                    location=location or 'Unassigned',
                    status='online',
                    fault_code='E00',
                    # 统一：数据库内部写入 UTC，避免服务器本地时区导致的偏移
                    last_heartbeat=datetime.utcnow()
                )
                db.session.add(device)
                db.session.flush()  # 获取ID但不提交，等待工单创建后一起提交
                logger.info(f"自动创建设备记录: {device_id}")
            except Exception as device_error:
                logger.error(f"创建设备记录时出错: {str(device_error)}")
                import traceback
                logger.error(traceback.format_exc())
                db.session.rollback()
                return None
        
        # 尝试直接查找（支持E01-E05格式和旧格式）
        fault_info = FAULT_KNOWLEDGE_GRAPH.get(fault_code)
        
        # 如果未找到，尝试通过FAULT_CODE_MAP转换
        if not fault_info and fault_code in FAULT_CODE_MAP:
            mapped_code = FAULT_CODE_MAP[fault_code]
            if mapped_code:
                fault_info = FAULT_KNOWLEDGE_GRAPH.get(mapped_code)
        
        if not fault_info:
            logger.warning(f"警告: 无法找到故障信息 {fault_code}")
            return None
        
        fault_name = fault_info.get('name', '未知故障')
        
        # 使用新的AI报告生成函数
        recommendation = generate_ai_report(fault_code)
        
        # 统一：数据库内部写入 UTC，避免因服务器本地时区设置导致 7/8 小时偏差
        if fault_time is None:
            fault_time = datetime.utcnow()
        
        work_order = WorkOrder(
            device_id=device_id,
            fault_time=fault_time,  # 使用服务器本地时间
            location=location or device.location,
            fault_type=fault_name,
            ai_recommendation=recommendation,
            status='pending'
        )
        
        db.session.add(work_order)
        # 注意：这里不提交，由调用者（node_heartbeat）在嵌套事务中提交
        # 这样可以确保原子性：检查-创建操作在同一事务中完成
        db.session.flush()  # 刷新到数据库但不提交，获取ID
        logger.info(f"工单已创建 (工单ID: {work_order.id}, 设备: {device_id})，等待提交")
        return work_order
    except Exception as e:
        logger.error(f"创建工单时出错: {str(e)}")
        import traceback
        logger.error(traceback.format_exc())
        db.session.rollback()
        return None

