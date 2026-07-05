"""
认证路由蓝图
处理登录、登出等认证相关功能
"""
from urllib.parse import urlparse, urljoin

from flask import Blueprint, render_template, request, redirect, url_for, flash, current_app
from flask_login import login_user, logout_user, login_required
from edgewind.models import User
from edgewind.extensions import limiter

auth_bp = Blueprint('auth', __name__)


def is_safe_redirect_url(target):
    """
    校验登录后的 next 重定向目标是否安全，防止 open-redirect 钓鱼攻击。

    只放行：
    - 站内相对路径（无 scheme、无 netloc），如 /monitor、/faults
    - 与当前请求 host 完全一致的绝对 URL

    拒绝：
    - 协议相对地址（//evil.com）——会被浏览器当作跨站跳转
    - 指向其它域名的绝对 URL（http://evil.com）
    - 非 http/https scheme（javascript:、data: 等）
    """
    if not target:
        return False

    # urljoin 以当前 host 为基准解析 target，能正确处理相对/绝对/协议相对地址
    host_url = request.host_url
    test_url = urlparse(urljoin(host_url, target))
    server_url = urlparse(host_url)

    # 只接受 http/https（urljoin 后站内相对路径 scheme 会补成 http/https）
    if test_url.scheme not in ('http', 'https'):
        return False

    # netloc 必须与当前请求的 host 完全一致，杜绝 //evil.com 与跨域绝对地址
    if test_url.netloc != server_url.netloc:
        return False

    return True


@auth_bp.route('/login', methods=['GET', 'POST'])
@limiter.limit("5 per minute", methods=["POST"])  # 仅限制登录提交，防暴力破解；GET 页面加载不限流
def login():
    """登录页面和处理"""
    if current_app.config.get('EDGEWIND_DISABLE_LOGIN'):
        next_page = request.args.get('next')
        if next_page and is_safe_redirect_url(next_page):
            return redirect(next_page)
        return redirect(url_for('pages.overview'))

    if request.method == 'POST':
        username = request.form.get('username')
        password = request.form.get('password')
        remember = request.form.get('remember') == 'on'  # 复选框：'on' 表示选中
        
        if not username or not password:
            flash('请输入用户名和密码', 'error')
            return render_template('login.html')
        
        user = User.query.filter_by(username=username).first()
        
        if user and user.check_password(password):
            # 使用 remember 参数，如果为 True，Flask-Login 会设置持久化 cookie
            login_user(user, remember=remember)
            next_page = request.args.get('next')
            # 校验 next 参数，只允许站内跳转，防止 open-redirect 钓鱼
            if next_page and is_safe_redirect_url(next_page):
                return redirect(next_page)
            return redirect(url_for('pages.overview'))
        else:
            flash('用户名或密码错误', 'error')
    
    return render_template('login.html')


@auth_bp.route('/logout')
@login_required
def logout():
    """登出"""
    logout_user()
    flash('您已成功登出', 'info')
    return redirect(url_for('auth.login'))

