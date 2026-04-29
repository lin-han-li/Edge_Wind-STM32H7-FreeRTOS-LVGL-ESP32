PC 侧资源写入（通过 SD 卡）

1) 生成图片与字体 .bin
   - 运行：python tools/gui_guider_pack.py
     生成：tools/qspi_assets/gui/*.bin
   - 生成字体（示例）：
     lv_font_conv --format bin --font "MDK-ARM/HARDWORK/EdgeWind_UI/fonts/SourceHanSerif.otf" \
       --size 12 --bpp 4 -o tools/qspi_assets/fonts/SourceHanSerifSC_Regular_12.bin
     lv_font_conv --format bin --font "MDK-ARM/HARDWORK/EdgeWind_UI/fonts/SourceHanSerif.otf" \
       --size 14 --bpp 4 -o tools/qspi_assets/fonts/SourceHanSerifSC_Regular_14.bin
     lv_font_conv --format bin --font "MDK-ARM/HARDWORK/EdgeWind_UI/fonts/SourceHanSerif.otf" \
       --size 16 --bpp 4 -o tools/qspi_assets/fonts/SourceHanSerifSC_Regular_16.bin
     lv_font_conv --format bin --font "MDK-ARM/HARDWORK/EdgeWind_UI/fonts/SourceHanSerif.otf" \
       --size 20 --bpp 4 -o tools/qspi_assets/fonts/SourceHanSerifSC_Regular_20.bin
     lv_font_conv --format bin --font "MDK-ARM/HARDWORK/EdgeWind_UI/fonts/SourceHanSerif.otf" \
       --size 30 --bpp 4 -o tools/qspi_assets/fonts/SourceHanSerifSC_Regular_30.bin

2) 拷贝到 SD 卡
   - 拷贝 tools/qspi_assets/gui → SD:/gui
   - 拷贝 tools/qspi_assets/fonts → SD:/fonts
   - 可选：拷贝 tools/qspi_assets/pinyin/pinyin_dict.bin → SD:/pinyin/pinyin_dict.bin
   - 可选：在 SD:/gui 放置 update.flag，强制板子重新同步

3) 上电/重启板子
   - 启动阶段会自动将 SD 资源同步到 QSPI（如 QSPI 无文件系统会自动格式化）
