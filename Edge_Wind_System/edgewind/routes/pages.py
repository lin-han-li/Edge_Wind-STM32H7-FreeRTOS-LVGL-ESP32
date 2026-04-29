"""
页面路由蓝图
处理前端页面渲染
"""
from flask import Blueprint, render_template
from flask_login import login_required

pages_bp = Blueprint('pages', __name__)


@pages_bp.route('/')
@login_required
def index():
    """主页面 - 重定向到概览"""
    return render_template('overview.html')


@pages_bp.route('/overview')
@login_required
def overview():
    """系统概览页面"""
    return render_template('overview.html')


@pages_bp.route('/monitor')
@login_required
def monitor():
    """实时监测页面"""
    return render_template('monitor.html')


@pages_bp.route('/history')
@login_required
def history():
    """历史曲线页面"""
    return render_template('history.html')


@pages_bp.route('/faults')
@login_required
def faults():
    """故障管理页面"""
    return render_template('faults.html')


@pages_bp.route('/settings')
@login_required
def settings():
    """系统设置页面"""
    return render_template('settings.html')


@pages_bp.route('/snapshots')
@login_required
def snapshots():
    """故障快照页面"""
    return render_template('snapshots.html')

