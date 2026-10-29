# NFC ePaper Display 生产固件

> 专为 **Seeed XIAO nRF54LM20A + 2.13" ePaper** 设计的 NFC 墨水屏固件。
> 用手机 NFC 一碰就能把图片或文字写到墨水屏上,断电不丢失,重启自动恢复。

## 这是什么

一块「NFC 电子墨水牌」:板子通电后初始化 NFC Type 4 Tag 与墨水屏,手机(用 NFC Tools 等App)靠近写入 NDEF 消息,固件解析后立即在墨水屏上完整刷新一次。支持:

- **文字**:NDEF Text 记录,LVGL 渲染显示。
- **图片**:自研 `XEPR` 协议,直接写入 ePaper framebuffer。
- **两种面板**:
  - 黑白 1-bit(250×122,256×122 对齐)— 已发布预编译固件
  - 四色 2bpp(GDEY0213F51 / JD79661 四色面板)— **正在完善中,尚未发布**
- **内容持久化**:最近一次写入的图片/文字存入独立内容区,重启自动恢复。
- **分段写入**:4 色大图可拆成 `XEPR2A` / `XEPR2B` 两段 Text 记录分次写入,自动拼合并 CRC 校验(适配不支持二进制 MIME 的写入App)。

> **关于四色固件**:四色(黑/白/红/黄)图片数据量约 8 KB,超出了 NFC 单次文本写入的容量上限。代码里已实现 `XEPR2A`/`XEPR2B` 分段写入方案来绕过该限制,但仍在完善,**暂不提供预编译固件下载**。欢迎协助完善(见 [GitHub Pages 说明页](https://lsw031105guge-bit.github.io/NFC-epaoer-display-produce-code/))。

## 目标硬件

| 项目 | 值 |
|---|---|
| 主控 | Seeed XIAO nRF54LM20A(nRF54LM20A,Cortex-M33) |
| 屏幕 | 2.13" ePaper(GDEY0213F51 / JD79661) |
| 调试器 | 板载 SAMD11(USB→SWD,CMSIS-DAP) |
| NFC | nRF54LM20A 内置 NFC Type 4 Tag |
| 框架 | nRF Connect SDK(Zephyr)|

## 固件下载与烧录

**预编译固件在 [`firmware/`](./firmware) 目录,可直接下载烧录,无需自己编译。`.hex` 是预编译机器码固件(成品),不是工程文件——烧进芯片即运行;若要改代码再取本仓库源码编译。**

| 固件 | 说明 | 文件 |
|---|---|---|
| 黑白(1-bit) | 标准 NDEF 文字/黑白图片(推荐) | [`firmware/xiao_nrf54lm20a_epaper_nfc_bw.hex`](./firmware/xiao_nrf54lm20a_epaper_nfc_bw.hex) |
| 四色(2bpp) | ⚠ 正在完善中,**暂不提供下载** | — |

### 烧录步骤

板子用板载 SAMD11 调试器,通过 **OpenOCD**(CMSIS-DAP / SWD)烧录,**不需要 J-Link**——一条 USB-C 数据线直连即可。`firmware/` 下已附自包含的 `openocd.cfg` 与一键脚本。

1. **装 openocd**:推荐装 [PlatformIO](https://platformio.org/),再装 Seeed platform(自带适配好的 openocd):
   ```bash
   pio pkg install -g -p "https://github.com/Seeed-Studio/platform-seeedboards.git"
   ```
   macOS 下把 openocd 加入 PATH:
   ```bash
   export PATH="$HOME/Library/Application Support/Seeed/OpenOCD/tool-openocd/bin:$PATH"
   ```

2. **USB-C 连接板子**。

3. **一键烧录**(下载 `firmware/` 下的 hex、`openocd.cfg`、`flash.sh`/`flash.bat` 后):
   ```bash
   # macOS / Linux
   chmod +x flash.sh
   ./flash.sh xiao_nrf54lm20a_epaper_nfc_bw.hex      # 黑白
   ```
   ```bat
   :: Windows
   flash.bat xiao_nrf54lm20a_epaper_nfc_bw.hex
   ```

   等价的手动命令:
   ```bash
   openocd -f firmware/openocd.cfg \
     -c "init" \
     -c "nrf54lm20a-load firmware/xiao_nrf54lm20a_epaper_nfc_bw.hex" \
     -c "reset" -c "exit"
   ```

### 从源码编译(可选)

需要 nRF Connect SDK(v3.3.0+,支持 nRF54LM20A)与 Seeed `platform-seeedboards` 板支持包:

```bash
# 黑白
west build -b xiao_nrf54lm20a/nrf54lm20a/cpuapp writable_ndef_msg_1
west flash

# 四色
west build -b xiao_nrf54lm20a/nrf54lm20a/cpuapp writable_ndef_msg_1 -- -DDTC_OVERLAY_FILE=boards/quadcolor.overlay
west flash
```

## 配套工具

- **[NFC ePaper Image Converter](https://github.com/lsw031105guge-bit/xiao-epaper-web)** — 纯网页工具:上传图片 → 自动转成 XEPR 数据 → 用手机 NFC Tools 写入本设备。固件与网页解耦,网页只产出数据。

## 源码结构

```
src/
├── main.c            主程序:NFC T4T + 欢迎屏 + 内容持久化
├── jd79661.{c,h}     JD79661 / GDEY0213F51 驱动(四色直写)
├── xepr.{c,h}        XEPR 图片协议解析与 repack
├── ndef_parser.{c,h} NDEF 消息解析
└── ndef_file_m.{c,h} NDEF 文件 / 内容存储管理
boards/               板级 overlay(含 quadcolor 四色 overlay、XIAO nRF54LM20A overlay)
dts/                  JD79661 设备树 binding
firmware/             预编译固件 + openocd.cfg + 烧录脚本
```

## 许可证

基于 Nordic Semiconductor 的 `writable_ndef_msg` sample 修改,源码遵循 `SPDX-License-Identifier: LicenseRef-Nordic-5-Clause`。新增驱动与协议代码沿用相同许可证。
