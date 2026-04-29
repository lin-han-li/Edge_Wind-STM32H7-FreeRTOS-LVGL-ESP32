"""
生产环境 WSGI 入口文件
用于 Gunicorn 启动服务器

使用方式：
    gunicorn --worker-class eventlet -w 4 --bind 0.0.0.0:5000 wsgi:application

或使用配置文件：
    gunicorn -c gunicorn_config.py wsgi:application
"""
from app import app, socketio

# Gunicorn 需要这个变量来启动服务器
# application 是 Gunicorn 的标准入口点
# 对于 Flask-SocketIO，直接使用 socketio 对象
application = socketio

