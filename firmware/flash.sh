#!/usr/bin/env bash
#
# XIAO nRF54LM20A 一键烧录脚本 (macOS / Linux)
#
# 用法:
#   ./flash.sh                       # 烧录默认的黑白(1-bit)固件
#   ./flash.sh xiao_nrf54lm20a_epaper_nfc_4color.hex   # 烧录 4 色固件
#
# 前置:
#   1. 用 USB-C 线连接 XIAO nRF54LM20A(板载 SAMD11 调试器会枚举为 CMSIS-DAP)
#   2. 已安装 openocd(支持 CMSIS-DAP,建议用 PlatformIO 的 Seeed platform 自带版本)
#      - 安装见: https://platformio.org/
#      - 装好 Seeed platform:
#          pio pkg install -g -p "https://github.com/Seeed-Studio/platform-seeedboards.git"
#      - 把 Seeed 的 openocd 加入 PATH(macOS 默认路径):
#          export PATH="$HOME/Library/Application Support/Seeed/OpenOCD/tool-openocd/bin:$PATH"

set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
HEX="${1:-xiao_nrf54lm20a_epaper_nfc_bw.hex}"
HEX_PATH="$DIR/$HEX"

if [ ! -f "$HEX_PATH" ]; then
    echo "错误: 找不到固件文件 $HEX_PATH" >&2
    echo "可用固件:" >&2
    ls "$DIR"/*.hex >&2
    exit 1
fi

if ! command -v openocd >/dev/null 2>&1; then
    echo "错误: 未找到 openocd。请先安装(见脚本头部的说明)。" >&2
    exit 1
fi

echo ">> 烧录: $HEX"
echo ">> 配置: $DIR/openocd.cfg"
openocd -f "$DIR/openocd.cfg" \
    -c "init" \
    -c "nrf54lm20a-load $HEX_PATH" \
    -c "reset" \
    -c "exit"
echo ">> 完成"
