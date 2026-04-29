【SD 卡资源拷贝说明】

1. 将本目录下的 gui/ 与 fonts/ 两个文件夹直接拷贝到 SD 卡根目录。
   即：
     - tools/sd_payload/gui/   → SD:/gui/
     - tools/sd_payload/fonts/ → SD:/fonts/

2. 如需强制板子同步资源到 QSPI Flash：
   在 SD:/gui 下放置 update.flag（空文件即可）。

3. 确认工程加载字库文件名：
   - SourceHanSerifSC_Regular_12.bin (日志小字，可选)
   - SourceHanSerifSC_Regular_14.bin (小字，可选)
   - SourceHanSerifSC_Regular_16.bin (小字，可选)
   - SourceHanSerifSC_Regular_20.bin (gui_assets.c)
   - SourceHanSerifSC_Regular_30.bin (状态栏，可选)

4. 拼音字典（可选，用于中文输入法）：
   - pinyin/pinyin_dict.bin
