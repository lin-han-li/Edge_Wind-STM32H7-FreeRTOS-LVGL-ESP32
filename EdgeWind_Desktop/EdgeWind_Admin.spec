# -*- mode: python ; coding: utf-8 -*-
from PyInstaller.utils.hooks import collect_submodules

hiddenimports = ['eventlet.hubs.epolls', 'eventlet.hubs.selects', 'eventlet.hubs.poll', 'eventlet.hubs.kqueue', 'app', 'engineio.async_drivers.threading', 'engineio.async_drivers.eventlet', 'engineio.async_drivers.gevent', 'flask_socketio', 'flask_wtf', 'flask_wtf.csrf', 'webview']
hiddenimports += collect_submodules('eventlet')


a = Analysis(
    ['run_desktop.py'],
    pathex=['C:\\Users\\pengjianzhong\\Desktop\\MY_Project\\EdgeWind_STM32_ESP32\\EdgeWind_Desktop\\..\\Edge_Wind_System'],
    binaries=[],
    datas=[('C:\\Users\\pengjianzhong\\Desktop\\MY_Project\\EdgeWind_STM32_ESP32\\EdgeWind_Desktop\\..\\Edge_Wind_System\\app.py', '.'), ('C:\\Users\\pengjianzhong\\Desktop\\MY_Project\\EdgeWind_STM32_ESP32\\EdgeWind_Desktop\\..\\Edge_Wind_System\\templates', 'templates'), ('C:\\Users\\pengjianzhong\\Desktop\\MY_Project\\EdgeWind_STM32_ESP32\\EdgeWind_Desktop\\..\\Edge_Wind_System\\static', 'static'), ('C:\\Users\\pengjianzhong\\Desktop\\MY_Project\\EdgeWind_STM32_ESP32\\EdgeWind_Desktop\\..\\Edge_Wind_System\\edgewind', 'edgewind'), ('C:\\Users\\pengjianzhong\\Desktop\\MY_Project\\EdgeWind_STM32_ESP32\\EdgeWind_Desktop\\.env', '.')],
    hiddenimports=hiddenimports,
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    noarchive=False,
    optimize=0,
)
pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.datas,
    [],
    name='EdgeWind_Admin',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    upx_exclude=[],
    runtime_tmpdir=None,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
    icon=['C:\\Users\\pengjianzhong\\Desktop\\MY_Project\\EdgeWind_STM32_ESP32\\EdgeWind_Desktop\\Admin_build.ico'],
)
