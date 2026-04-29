"""
数据库模型模块
定义所有SQLAlchemy数据库模型
"""
from flask_sqlalchemy import SQLAlchemy
from flask_login import UserMixin
from werkzeug.security import generate_password_hash, check_password_hash
from datetime import datetime
import json
import re

db = SQLAlchemy()

# ==================== 密码验证工具函数 ====================
def validate_password(password, config):
    """
    验证密码复杂度
    
    Args:
        password: 待验证的密码
        config: Flask config对象
    
    Returns:
        tuple: (is_valid: bool, message: str)
    """
    # 检查长度
    if len(password) < config['PASSWORD_MIN_LENGTH']:
        return False, f"密码长度至少{config['PASSWORD_MIN_LENGTH']}位"
    
    # 检查大写字母
    if config['PASSWORD_REQUIRE_UPPERCASE']:
        if not re.search(r'[A-Z]', password):
            return False, "密码必须包含至少一个大写字母"
    
    # 检查数字
    if config['PASSWORD_REQUIRE_DIGITS']:
        if not re.search(r'\d', password):
            return False, "密码必须包含至少一个数字"
    
    # 检查特殊字符
    if config['PASSWORD_REQUIRE_SPECIAL']:
        if not re.search(r'[!@#$%^&*(),.?":{}|<>]', password):
            return False, "密码必须包含至少一个特殊字符"
    
    return True, "密码符合要求"


# ==================== 数据库模型 ====================

class User(UserMixin, db.Model):
    """用户表 - 用于身份认证"""
    __tablename__ = 'users'
    
    id = db.Column(db.Integer, primary_key=True)
    username = db.Column(db.String(100), unique=True, nullable=False)
    password_hash = db.Column(db.String(200))
    
    def set_password(self, password, config):
        """设置密码（自动哈希，包含复杂度验证）"""
        # 验证密码复杂度
        is_valid, message = validate_password(password, config)
        if not is_valid:
            raise ValueError(message)
        
        self.password_hash = generate_password_hash(password)
    
    def check_password(self, password):
        """验证密码"""
        return check_password_hash(self.password_hash, password)


class Device(db.Model):
    """设备节点表"""
    __tablename__ = 'devices'
    
    id = db.Column(db.Integer, primary_key=True)
    device_id = db.Column(db.String(100), unique=True, nullable=False, index=True)
    location = db.Column(db.String(200), nullable=False)
    hw_version = db.Column(db.String(50))
    status = db.Column(db.String(20), default='online')  # online, offline, faulty
    fault_code = db.Column(db.String(20), default='E00')  # 当前故障代码，用于检测状态转换
    last_heartbeat = db.Column(db.DateTime, default=datetime.utcnow)
    registered_at = db.Column(db.DateTime, default=datetime.utcnow)
    
    # 关联数据点和工单
    datapoints = db.relationship('DataPoint', backref='device', lazy=True, cascade='all, delete-orphan')
    work_orders = db.relationship('WorkOrder', backref='device', lazy=True, cascade='all, delete-orphan')


class DataPoint(db.Model):
    """数据点表 - 存储ADC波形数据"""
    __tablename__ = 'datapoints'
    
    id = db.Column(db.Integer, primary_key=True)
    device_id = db.Column(db.String(100), db.ForeignKey('devices.device_id'), nullable=False)
    timestamp = db.Column(db.DateTime, default=datetime.utcnow, index=True)
    waveform = db.Column(db.Text)  # JSON格式存储波形数据（1024个浮点数数组）
    status = db.Column(db.String(20), default='normal')  # normal, fault
    fault_code = db.Column(db.String(50))  # 故障代码 E00=Normal, E01=AC Intrusion, E02=Insulation Fault


class WorkOrder(db.Model):
    """工单表"""
    __tablename__ = 'work_orders'
    
    id = db.Column(db.Integer, primary_key=True)
    device_id = db.Column(db.String(100), db.ForeignKey('devices.device_id'), nullable=False)
    fault_time = db.Column(db.DateTime, default=datetime.utcnow, index=True)
    location = db.Column(db.String(200))
    fault_type = db.Column(db.String(100))
    ai_recommendation = db.Column(db.Text)
    status = db.Column(db.String(20), default='pending')  # pending, processing, fixed


class SystemConfig(db.Model):
    """系统配置表"""
    __tablename__ = 'system_config'
    
    id = db.Column(db.Integer, primary_key=True)
    key = db.Column(db.String(100), unique=True, nullable=False, index=True)
    value = db.Column(db.Text)  # JSON格式存储配置值
    description = db.Column(db.String(200))
    updated_at = db.Column(db.DateTime, default=datetime.utcnow, onupdate=datetime.utcnow)


class FaultSnapshot(db.Model):
    """故障快照表 - 保存故障前后的波形数据"""
    __tablename__ = 'fault_snapshots'
    
    id = db.Column(db.Integer, primary_key=True)
    device_id = db.Column(db.String(100), db.ForeignKey('devices.device_id'), nullable=False, index=True)
    fault_code = db.Column(db.String(10), nullable=False, index=True)  # 故障代码 E01-E05
    snapshot_type = db.Column(db.String(20), nullable=False)  # 'before' 或 'after'
    timestamp = db.Column(db.DateTime, default=datetime.utcnow, index=True)
    
    # 通道信息
    channel_id = db.Column(db.Integer, nullable=False)  # 0-3
    channel_label = db.Column(db.String(50))  # 通道名称
    channel_type = db.Column(db.String(20))  # 'DC', 'Current', 'Leakage'
    
    # 数据快照
    current_value = db.Column(db.Float)  # 当前瞬时值
    waveform_data = db.Column(db.Text)  # JSON格式的波形数据 (1024点)
    fft_data = db.Column(db.Text)  # JSON格式的FFT频谱数据 (115点)
    
    # 统计信息
    mean_value = db.Column(db.Float)  # 平均值
    std_value = db.Column(db.Float)  # 标准差
    max_value = db.Column(db.Float)  # 最大值
    min_value = db.Column(db.Float)  # 最小值
    
    # 关联到设备
    device = db.relationship('Device', backref='fault_snapshots')
    
    def to_dict(self):
        """转换为字典格式"""
        # 统一：对外展示使用北京时间（Asia/Shanghai）
        from edgewind.time_utils import fmt_beijing
        
        return {
            'id': self.id,
            'device_id': self.device_id,
            'fault_code': self.fault_code,
            'snapshot_type': self.snapshot_type,
            'timestamp': fmt_beijing(self.timestamp),
            'channel_id': self.channel_id,
            'channel_label': self.channel_label,
            'channel_type': self.channel_type,
            'current_value': self.current_value,
            'waveform_data': json.loads(self.waveform_data) if self.waveform_data else [],
            'fft_data': json.loads(self.fft_data) if self.fft_data else [],
            'statistics': {
                'mean': self.mean_value,
                'std': self.std_value,
                'max': self.max_value,
                'min': self.min_value
            }
        }


class HistoryData(db.Model):
    """历史数据表 - 存储每帧上报的4通道平均值，用于历史曲线回放"""
    __tablename__ = 'history_data'
    
    id = db.Column(db.Integer, primary_key=True)
    device_id = db.Column(db.String(100), nullable=False, index=True)
    timestamp = db.Column(db.DateTime, default=datetime.utcnow, index=True)
    
    # 4通道平均值
    voltage_pos = db.Column(db.Float)   # 直流母线(+)
    voltage_neg = db.Column(db.Float)   # 直流母线(-)
    current = db.Column(db.Float)       # 负载电流
    leakage = db.Column(db.Float)       # 漏电流
    
    def to_dict(self):
        """转换为字典格式（时间为北京时间）"""
        from edgewind.time_utils import iso_beijing
        return {
            'id': self.id,
            'device_id': self.device_id,
            'timestamp': iso_beijing(self.timestamp, with_seconds=True),
            'voltage_pos': self.voltage_pos,
            'voltage_neg': self.voltage_neg,
            'current': self.current,
            'leakage': self.leakage
        }

