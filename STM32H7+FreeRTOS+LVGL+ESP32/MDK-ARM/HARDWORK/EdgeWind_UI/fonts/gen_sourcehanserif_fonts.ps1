param(
    # Put SourceHanSerif OTF/TTF here, or pass absolute path
    [string]$FontPath = (Join-Path $PSScriptRoot "SourceHanSerif.otf"),

    # Font sizes to generate (project currently uses 14/16/20)
    [int[]]$Sizes = @(14, 16, 20),

    # bpp=4 is a good default
    [int]$Bpp = 4
)

$ErrorActionPreference = "Stop"

$charsFile = Join-Path $PSScriptRoot "chars_cn.txt"

if(!(Test-Path $FontPath)) {
    throw "Font file not found: $FontPath. Copy SourceHanSerif OTF/TTF here or pass -FontPath 'C:\path\SourceHanSerif.otf'"
}
if(!(Test-Path $charsFile)) {
    throw "chars file not found: $charsFile"
}

function Ensure-LvFontConv {
    $cmd = Get-Command lv_font_conv -ErrorAction SilentlyContinue
    if($null -ne $cmd) { return }

    Write-Host "Installing lv_font_conv: npm install -g lv_font_conv"
    npm install -g lv_font_conv
}

Ensure-LvFontConv

foreach($size in $Sizes) {
    $out = Join-Path $PSScriptRoot ("lv_font_SourceHanSerifSC_Regular_{0}.c" -f $size)

    Write-Host ("Generating size {0} -> {1}" -f $size, $out)

    # lv_font_conv (npm) has no --text-file, use --symbols
    # Read chars_cn.txt and remove whitespace/newlines
    $symbols = Get-Content -Raw -Encoding utf8 $charsFile
    $symbols = ($symbols -replace "\\s+", "")

    lv_font_conv --no-compress --format lvgl `
        --font "$FontPath" `
        --bpp $Bpp `
        --size $size `
        --symbols "$symbols" `
        --range 0x20-0x7F `
        --lv-font-name ("lv_font_SourceHanSerifSC_Regular_{0}" -f $size) `
        -o "$out"
}

Write-Host "Done. Keil project already includes the 14/16 .c files."
