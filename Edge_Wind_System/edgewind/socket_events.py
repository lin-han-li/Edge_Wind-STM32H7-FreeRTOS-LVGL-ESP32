"""
WebSocket事件处理模块
处理SocketIO实时通信事件
"""
from flask import request
from flask_socketio import emit, join_room, leave_room, disconnect
from flask_login import current_user
import time
import logging

logger = logging.getLogger(__name__)

# 全局变量（将从app传入）
client_subscriptions = {}  # {session_id: set of node_ids}
active_nodes = {}
# 与后端 active_nodes 口径一致：默认 60s，可用 EDGEWIND_NODE_TIMEOUT_SEC 调整
NODE_TIMEOUT = max(10, int((__import__("os").environ.get("EDGEWIND_NODE_TIMEOUT_SEC", "60") or "60")))


def init_socket_events(socketio, nodes):
    """初始化Socket事件处理器"""
    global active_nodes
    active_nodes = nodes
    
    @socketio.on('connect')
    def handle_connect():
        """客户端连接事件"""
        # 安全：局域网环境也可能被同网段扫描/连接，Socket.IO 必须要求已登录
        if not getattr(current_user, 'is_authenticated', False):
            logger.warning(f"⚠️ 未登录的Socket连接被拒绝: {request.sid}")
            disconnect()
            return

        sid = request.sid
        client_subscriptions[sid] = set()
        logger.info(f"✅ 客户端连接: {sid}")
        
        # 发送当前所有节点的状态摘要（轻量级）
        node_status_list = []
        current_time = time.time()
        for node_id, node_data in active_nodes.items():
            if current_time - node_data['timestamp'] < NODE_TIMEOUT:
                node_status_list.append({
                    'node_id': node_id,
                    'status': node_data.get('status', 'online'),
                    'fault_code': node_data.get('fault_code', 'E00'),
                    'timestamp': node_data['timestamp'],
                    'report_mode': (node_data.get('data', {}) or {}).get('report_mode'),
                    'downsample_step': (node_data.get('data', {}) or {}).get('downsample_step'),
                    'upload_points': (node_data.get('data', {}) or {}).get('upload_points'),
                })
        
        emit('node_status_list', {'nodes': node_status_list})

    @socketio.on('disconnect')
    def handle_disconnect():
        """客户端断开连接事件"""
        sid = request.sid
        if sid in client_subscriptions:
            # 清理订阅记录
            subscribed_nodes = client_subscriptions.pop(sid)
            logger.info(f"❌ 客户端断开: {sid}, 取消订阅: {subscribed_nodes}")

    @socketio.on('subscribe_node')
    def handle_subscribe_node(data):
        """客户端订阅特定节点的完整数据"""
        if not getattr(current_user, 'is_authenticated', False):
            # 双保险：防止未登录者绕过 connect 阶段校验
            disconnect()
            return

        sid = request.sid
        node_id = data.get('node_id')
        
        if not node_id:
            emit('error', {'message': '缺少 node_id 参数'})
            return
        
        # 加入房间（房间名为节点ID）
        join_room(f'node_{node_id}')
        
        # 记录订阅
        if sid not in client_subscriptions:
            client_subscriptions[sid] = set()
        client_subscriptions[sid].add(node_id)
        
        logger.info(f"📡 客户端 {sid} 订阅节点: {node_id}")
        
        # 立即发送该节点的最新完整数据（如果存在）
        if node_id in active_nodes:
            node_data = active_nodes[node_id]
            emit('monitor_update', {
                'node_id': node_id,
                'data': node_data.get('data', {}),
                'fault_code': node_data.get('fault_code', 'E00'),
                'is_initial': True  # 标记这是初始订阅数据，不应触发弹窗
            })

    @socketio.on('unsubscribe_node')
    def handle_unsubscribe_node(data):
        """客户端取消订阅特定节点"""
        sid = request.sid
        node_id = data.get('node_id')
        
        if not node_id:
            return
        
        # 离开房间
        leave_room(f'node_{node_id}')
        
        # 移除订阅记录
        if sid in client_subscriptions and node_id in client_subscriptions[sid]:
            client_subscriptions[sid].remove(node_id)
            logger.info(f"📡 客户端 {sid} 取消订阅节点: {node_id}")
