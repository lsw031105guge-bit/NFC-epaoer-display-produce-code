# 烧录教程(XIAO nRF54LM20A + ePaper 固件)

> 这份教程教你把 `.hex` 固件烧进 XIAO nRF54LM20A 板子。
> **不需要 J-Link,不需要编译,一根 USB-C 线 + 一条命令就行。**
>
> 适用系统:macOS / Windows / Linux。下面以 macOS 为主讲,Windows 在对应步骤给出 `.bat`。

---

## 0. 先搞懂:hex 是什么,烧录是干嘛

- **`.hex` 文件** = 做好的"成品软件"(机器码)。你不用编译代码,直接拿来就能用。
- **烧录** = 把这个成品软件,通过 USB 线,灌进芯片里,让它跑起来。
- **怎么灌?** 用一个小工具叫 `openocd`。它当"漏斗",把 hex 倒进芯片。
- **板子自带调试器**:XIAO nRF54LM20A 板上有个小芯片(SAMD11),它就是一个 USB 调试器。所以你**不用买 J-Link**,插根 USB-C 线就行。

一句话:**hex 是成品,openocd 是漏斗,板子自带漏斗口。**

---

## 1. 你需要准备什么

| 东西 | 说明 |
|---|---|
| XIAO nRF54LM20A 板 | 接好 ePaper 屏的那块 |
| **USB-C 数据线** | ⚠ 必须是能传数据的线,不是纯充电线!很多人卡在这 |
| 电脑 | macOS / Windows / Linux 都行 |
| `openocd` 工具 | 见第 2 步安装 |
| 固件 + 脚本 | 从 [GitHub Pages](https://lsw031105guge-bit.github.io/NFC-epaoer-display-produce-code/) 或本仓库 `firmware/` 目录下载 |

下载下来你会得到这几个文件,放同一个文件夹:

```
你的文件夹/
├── xiao_nrf54lm20a_epaper_nfc_bw.hex   ← 成品固件
├── openocd.cfg                          ← 烧录配置(告诉 openocd 怎么连板子)
├── flash.sh                             ← macOS/Linux 一键脚本
└── flash.bat                            ← Windows 一键脚本
```

---

## 2. 装 openocd(只装一次,以后不用再装)

推荐用 **PlatformIO** 装 Seeed 的 openocd,因为它自带对 nRF54LM20A 的支持,版本也对。

### 2.1 装 PlatformIO

1. 装 [VS Code](https://code.visualstudio.com/)(如果还没有)。
2. 在 VS Code 扩展商店搜 `platformio`,装 **PlatformIO IDE** 插件。
3. 装好后,终端里就有 `pio` 命令了。验证:
   ```bash
   pio --version
   ```

### 2.2 装 Seeed platform(含 openocd)

终端里跑:
```bash
pio pkg install -g -p "https://github.com/Seeed-Studio/platform-seeedboards.git"
```

装完,openocd 就在下面这个路径里了:

- **macOS**:`~/Library/Application Support/Seeed/OpenOCD/tool-openocd/bin/`
- **Windows**:`%USERPROFILE%\.platformio\packages\tool-openocd\bin\`(大致)

### 2.3 把 openocd 加进 PATH(关键一步,别跳)

不加 PATH 的话,后面会报 `未找到 openocd`。

**macOS / Linux** —— 编辑 `~/.zshrc`(或 `~/.bashrc`),加一行:
```bash
export PATH="$HOME/Library/Application Support/Seeed/OpenOCD/tool-openocd/bin:$PATH"
```
然后让它生效:
```bash
source ~/.zshrc
```

**Windows** —— 把 openocd 所在文件夹加到系统 PATH:
1. 开始菜单搜"环境变量" → 编辑系统环境变量 → 环境变量
2. 在"用户变量"里找 `Path` → 编辑 → 新建 → 粘贴 openocd 的 `bin` 文件夹路径
3. 一路确定,重启终端

### 2.4 验证装好了

```bash
openocd --version
```
能打印版本号就成了。

> 已经装过的人(本机已检测到 Seeed openocd)直接跳到第 3 步。

---

## 3. 插板子

用 **USB-C 数据线**,把 XIAO nRF54LM20A 插到电脑。

板子上的 SAMD11 会自动变成一个调试器,Mac 不用装驱动,Windows 第一次可能自动装驱动,等几秒。

怎么确认插好了:
- **macOS**:终端跑 `system_profiler SPUSBDataType | grep -iE "CMSIS-DAP|SAMD11|Seeed|XIAO"`,有输出就是连上了。
- **Windows**:设备管理器里能看到一个 CMSIS-DAP 设备。

看不到?八成是**线的问题**(换根能传数据的 USB-C 线),或板子没供电。

---

## 4. 烧录

### 4.1 一键烧录(推荐)

在放固件和脚本的文件夹里开终端:

**macOS / Linux:**
```bash
chmod +x flash.sh          # 第一次跑要加执行权限
./flash.sh                 # 默认烧黑白固件
```

**Windows:**
```bat
flash.bat
```

脚本会自动:打开 openocd → 灌固件 → 重启板子。看到 `>> 完成` 就成了。

### 4.2 手动烧录(想看细节的)

```bash
openocd -f openocd.cfg \
  -c "init" \
  -c "nrf54lm20a-load xiao_nrf54lm20a_epaper_nfc_bw.hex" \
  -c "reset" \
  -c "exit"
```

---

## 5. 烧完什么样

板子自动重启,ePaper 屏会先显示一个**欢迎画面**,说明固件跑起来了。

接下来就可以拿手机往它上面写内容了(见第 6 步)。

---

## 6. 用手机 NFC 写内容(可选,烧完之后)

1. 手机装个 NFC App,推荐 **NFC Tools**(iOS / Android 都有)。
2. 配合网页工具 [NFC ePaper Image Converter](https://github.com/lsw031105guge-bit/xiao-epaper-web):上传图片 → 它给你一段数据。
3. 手机 NFC Tools → 写入 → 把数据作为 NDEF Text 记录写进去。
4. 手机背面对着板子贴一下 → 屏幕刷新 → 图就上去了。

断电不丢,重启自动恢复上次内容。

---

## 7. 常见问题(出错先看这)

### Q: `openocd: command not found` / `未找到 openocd`
openocd 没在 PATH 里。回第 2.3 步,把 openocd 的 `bin` 目录加进 PATH,然后重开终端。

### Q: 板子插了但 openocd 报 `Error: unable to open CMSIS-DAP` / 找不到设备
- 换一根 **能传数据的** USB-C 线(最常见的坑)。
- 换一个 USB 口。
- macOS 第一次可能要在"系统设置 → 隐私与安全性"里允许这个 USB 设备。
- Windows 检查设备管理器里有没有 CMSIS-DAP,没有就是线/驱动问题。

### Q: 报 `Error: ... nrf54lm20a-load ...` / 烧到一半失败
- 确认用的是本仓库 `firmware/openocd.cfg`(它里面有针对 nRF54LM20A 的特殊处理:写完要 commit write buffer)。
- 换个 USB 口,板子供电要稳。

### Q: 烧完屏幕没反应
- 确认 ePaper 屏的排线插对了方向、插紧。
- 按一下板子上的复位键(RST),或拔插一次 USB。

### Q: 我想烧别的 hex 文件
把文件名传给脚本:
```bash
./flash.sh 你的固件.hex     # macOS
flash.bat 你的固件.hex       # Windows
```

### Q: 下载的 hex 是工程文件吗?
不是。`.hex` 是预编译好的机器码固件,是"成品",直接烧就能跑。工程文件是源码(在仓库 `src/` 里),给想改代码的人用。只是烧录的话,只用 `.hex` 就够。

---

## 附:文件说明

| 文件 | 作用 |
|---|---|
| `xiao_nrf54lm20a_epaper_nfc_bw.hex` | 黑白固件(成品,推荐) |
| `openocd.cfg` | 烧录配置,自包含,可单独用 |
| `flash.sh` | macOS/Linux 一键脚本 |
| `flash.bat` | Windows 一键脚本 |

四色固件正在完善中(数据量超 NFC 单次写入上限),暂不提供下载,见 [GitHub Pages 说明页](https://lsw031105guge-bit.github.io/NFC-epaoer-display-produce-code/)。

---

卡在哪一步,把那一步的报错截图发我。
