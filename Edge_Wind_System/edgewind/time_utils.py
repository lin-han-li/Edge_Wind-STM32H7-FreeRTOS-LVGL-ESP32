"""
时间工具：统一使用北京时间（Asia/Shanghai）显示，数据库内部统一按 UTC 写入。

说明：
- SQLite/SQLAlchemy 默认不保存时区信息，项目中历史上混用了 datetime.now() 与 datetime.utcnow()
  以及手工 timedelta(hours=8) 的方式，容易造成“有的显示北京时间、有的差 7/8 小时”的混乱。
- 本模块约定：**所有存入数据库的时间都使用 UTC（datetime.utcnow）**；
  **所有返回给前端展示的时间都转换为北京时间（Asia/Shanghai，UTC+8）**。
"""

from __future__ import annotations

from datetime import datetime, timezone, timedelta

try:
    # Python 3.9+
    from zoneinfo import ZoneInfo  # type: ignore
except Exception:  # pragma: no cover
    ZoneInfo = None  # type: ignore


# 统一使用北京时间：优先使用 IANA 时区（Asia/Shanghai）；若系统缺少 tzdata，则回退到固定 UTC+8。
# 说明：Windows 某些精简环境可能没有系统时区库；而北京时间不使用夏令时，固定偏移即可满足项目需求。
BEIJING_TZ = timezone(timedelta(hours=8))
if ZoneInfo:
    try:
        BEIJING_TZ = ZoneInfo("Asia/Shanghai")
    except Exception:
        # 缺少 tzdata / 无法加载时区信息时回退到 UTC+8
        BEIJING_TZ = timezone(timedelta(hours=8))


def to_utc(dt: datetime | None) -> datetime | None:
    """将时间视为 UTC（若无 tzinfo 则按 UTC 解释），返回带 tzinfo=UTC 的 datetime。"""
    if not dt:
        return None
    if dt.tzinfo is None:
        return dt.replace(tzinfo=timezone.utc)
    return dt.astimezone(timezone.utc)


def to_beijing(dt: datetime | None) -> datetime | None:
    """将时间转换为北京时间（Asia/Shanghai）。"""
    utc_dt = to_utc(dt)
    if not utc_dt:
        return None
    return utc_dt.astimezone(BEIJING_TZ)


def fmt_beijing(dt: datetime | None, with_seconds: bool = True) -> str | None:
    """格式化为北京时间字符串：YYYY-MM-DD HH:MM(:SS)。"""
    bj = to_beijing(dt)
    if not bj:
        return None
    return bj.strftime("%Y-%m-%d %H:%M:%S" if with_seconds else "%Y-%m-%d %H:%M")


def iso_beijing(dt: datetime | None, with_seconds: bool = True) -> str | None:
    """
    格式化为带时区偏移的 ISO 字符串（北京时间）：YYYY-MM-DDTHH:MM(:SS)+08:00
    用于前端 new Date(iso) 解析时不产生二次时区偏移。
    """
    bj = to_beijing(dt)
    if not bj:
        return None
    # %z 形如 +0800，转换为 +08:00
    s = bj.strftime("%Y-%m-%dT%H:%M:%S%z" if with_seconds else "%Y-%m-%dT%H:%M%z")
    return s[:-2] + ":" + s[-2:]


