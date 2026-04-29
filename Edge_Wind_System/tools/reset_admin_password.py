"""
离线重置管理员密码（不启动服务器）。

用法示例（PowerShell）：
  .\venv311\Scripts\python.exe .\tools\reset_admin_password.py --password "Gentle9532"
  .\venv311\Scripts\python.exe .\tools\reset_admin_password.py --username "Edge_Wind" --password "Gentle9532"

也支持环境变量：
  $env:EDGEWIND_ADMIN_USERNAME="Edge_Wind"
  $env:EDGEWIND_ADMIN_NEW_PASSWORD="Gentle9532"
  .\venv311\Scripts\python.exe .\tools\reset_admin_password.py
"""

from __future__ import annotations

import argparse
import os
import sys

# 确保可从 tools/ 子目录运行时也能导入项目包（edgewind）
PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), os.pardir))
if PROJECT_ROOT not in sys.path:
    sys.path.insert(0, PROJECT_ROOT)

from flask import Flask

from edgewind.config import Config
from edgewind.models import db, User


def build_app() -> Flask:
    app = Flask(__name__)
    app.config.from_object(Config)
    db.init_app(app)
    return app


def parse_args(argv: list[str]) -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Reset EdgeWind admin password (offline).")
    p.add_argument(
        "--username",
        default=(os.environ.get("EDGEWIND_ADMIN_USERNAME") or "Edge_Wind").strip() or "Edge_Wind",
        help="Admin username (default: env EDGEWIND_ADMIN_USERNAME or 'Edge_Wind')",
    )
    p.add_argument(
        "--password",
        default=(os.environ.get("EDGEWIND_ADMIN_NEW_PASSWORD") or "").strip(),
        help="New password (default: env EDGEWIND_ADMIN_NEW_PASSWORD).",
    )
    return p.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)

    if not args.password:
        print(
            "缺少新密码：请传入 --password 或设置环境变量 EDGEWIND_ADMIN_NEW_PASSWORD",
            file=sys.stderr,
        )
        return 2

    app = build_app()

    with app.app_context():
        # 确保表存在（不会覆盖既有数据）
        db.create_all()

        user = User.query.filter_by(username=args.username).first()
        created = False
        if not user:
            user = User(username=args.username)
            db.session.add(user)
            created = True

        try:
            user.set_password(args.password, app.config)
        except ValueError as e:
            print(f"密码不符合当前策略：{e}", file=sys.stderr)
            db.session.rollback()
            return 3

        db.session.commit()

        action = "已创建并设置密码" if created else "已重置密码"
        print(f"{action}：{args.username}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

