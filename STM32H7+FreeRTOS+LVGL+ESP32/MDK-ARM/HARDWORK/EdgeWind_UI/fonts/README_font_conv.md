## LVGL 9.4.0 思源宋体字库生成（Node.js / lv_font_conv）

你已全局安装：

```bash
npm install -g lv_font_conv
```

### 1) 准备字体文件

请将 **思源宋体（Source Han Serif SC）** 的字体文件复制到本目录，并命名为：

- `SourceHanSerif.otf`

或者你也可以不放到这里，直接在命令/脚本里用 `-FontPath` 指定绝对路径。

### 2) 准备字符清单

本目录下的 `chars_cn.txt` 已包含当前项目 UI 需要显示的中文字符。
如你后续新增 UI 文案，请把新增中文直接追加到该文件。

### 3) 一键生成 14/16 字号字库（推荐）

在本目录打开 PowerShell，执行：

```powershell
.\gen_sourcehanserif_fonts.ps1
```

自定义字体路径（示例）：

```powershell
.\gen_sourcehanserif_fonts.ps1 -FontPath "D:\Fonts\SourceHanSerifSC-Regular.otf"
```

自定义字号（示例：额外生成 20px）：

```powershell
.\gen_sourcehanserif_fonts.ps1 -Sizes 14,16,20
```

### 4) 输出文件

脚本会生成/覆盖：

- `lv_font_SourceHanSerifSC_Regular_14.c`
- `lv_font_SourceHanSerifSC_Regular_16.c`

并保持字体变量名为：

- `lv_font_SourceHanSerifSC_Regular_14`
- `lv_font_SourceHanSerifSC_Regular_16`

> 这样工程里 `fonts/ew_fonts.h` 的 `LV_FONT_DECLARE(...)` 不需要改动。

