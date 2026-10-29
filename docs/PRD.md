# PRD：XIAO NFC + ePaper 固件

> 项目代号：`writable_ndef_msg_1`
> 硬件平台：XIAO nRF54LM20A + NFC 天线 + Seeed ePaper Driver Board + 2.13" 单色墨水屏
> 代码基础：Nordic `writable_ndef_msg` sample（已跑通 NFC 可写 NDEF）

---

## 1. 产品目标

设备上电后显示欢迎页面：

```
NFC ePaper
Tap phone to update
[QR Code]      ← 暂无网页，占位
Website URL    ← 暂无，预留 nfc-eink.seeed.cc
```

网页地址未定，第一版直接显示：

```
NFC ePaper Demo

Ready for NFC

Waiting for image...
```

用户用手机（NFC Tools / 网页 Web NFC）向设备写入内容：
- **第一阶段**：Text NDEF（如 `Hello XIAO`），设备解析后显示在墨水屏上。
- **第二阶段**：MIME NDEF（`application/x-xiao-epaper`），内含 XEPR 格式的 1-bit 位图，设备直接刷屏。

---

## 2. 硬件

| 组件 | 说明 |
|---|---|
| 主控 | XIAO nRF54LM20A |
| NFC | 板载 NFC 天线（Type 4 Tag emulation） |
| 显示 | Seeed ePaper Driver Board + 2.13" 单色 eInk |
| 驱动 IC | SSD1680（GDEY0213B74） |
| 总线 | SPI，4 MHz |
| 引脚 | DC、RESET、BUSY、CS 等 GPIO，见设备树 overlay |

分辨率 250×122（约 3904 bytes bitmap，与 PRD 中 XEPR payload 大小对应；最终以驱动实测为准）。

---

## 3. 开机逻辑

```
Power ON
  ↓
初始化 Display（SSD1680 / LVGL）
  ↓
初始化 Flash / NVS
  ↓
读取上次保存的 NDEF
  ↓
有有效图片？ ── 有 → 显示上一次图片
  │
  无 → 显示 Welcome / Setup 页面
  ↓
启动 NFC T4T emulation，进入 WAITING
```

---

## 4. 初始提示画面（V0.1 目标）

```
┌──────────────────────────────┐
│                              │
│       NFC ePaper             │
│                              │
│     Tap phone to update      │
│                              │
│       [ QR Code ]            │
│                              │
│       Website URL            │
│                              │
└──────────────────────────────┘
```

网页暂无时降级为：

```
NFC ePaper Demo

Ready for NFC

Waiting for image...
```

---

## 5. NFC 逻辑（基于现有 sample 改造）

现有代码已实现，直接复用：

```c
ndef_file_setup();
ndef_file_load(ndef_msg_buf, sizeof(ndef_msg_buf));
nfc_t4t_setup(nfc_callback, NULL);
nfc_t4t_ndef_rwpayload_set(ndef_msg_buf, sizeof(ndef_msg_buf));
nfc_t4t_emulation_start();
```

### 5.1 NFC 回调中不做刷屏

收到 `NFC_T4T_EVENT_NDEF_UPDATED` 时**不刷新 ePaper**（ePaper 刷屏慢且占用总线，不能在 NFC 中断上下文里做），只做：

```c
case NFC_T4T_EVENT_NDEF_UPDATED:
    copy_ndef_buffer();            /* 复制 ndef_msg_buf 到工作区 */
    k_sem_give(&ndef_update_sem);  /* 通知 worker thread */
    break;
```

注意：现有 sample 在回调里已经有 `flash_buffer_prepare()` + 信号量的模式，可以在此基础上改造，避免在 NFC 事件里直接刷屏或写 Flash。

---

## 6. Worker Task

```
k_sem_take(ndef_update_sem)
  ↓
解析 NDEF
  ↓
检查 Record Type（Text / MIME）
  ↓
取 Payload
  ↓
验证 magic / size / CRC（图片阶段）
  ↓
保存 Flash（NVS）
  ↓
刷新 ePaper
```

结构：

```c
while (1) {
    k_sem_take(&ndef_update_sem, K_FOREVER);

    if (parse_ndef()) {
        if (validate_epaper_payload()) {   /* V0.3 起可先跳过校验 */
            save_image();
            display_image();
        }
    }
}
```

---

## 7. NDEF Parser

- **V0.3（第一版）**：Text Record，UTF-8，取 payload 为字符串，直接 `lv_label_set_text()` 显示。
- **V0.4+（第二版）**：MIME Record，type = `application/x-xiao-epaper`，payload 为 XEPR 数据。

### 7.1 Text 测试流程（必须先做通）

```
手机 NFC Tools
  → 写 Text NDEF "Hello XIAO"
  → XIAO 收到 NDEF_UPDATED
  → Extract Text
  → LVGL label
  → ePaper 显示 "Hello XIAO"
```

---

## 8. 图片阶段（V0.4+）

### 8.1 网页端（另见 PRD 1，Web App）

```
Image → Crop → Resize → Dither → 1-bit Bitmap → XEPR Payload
```

MCU **不做**图片算法，只接收现成位图。

### 8.2 设备端

```
NDEF Message
  → MIME Record（application/x-xiao-epaper）
  → Payload = XEPR
  → 解析 XEPR header（magic / size / CRC）
  → 3904 bytes bitmap
  → framebuffer → display_write / LVGL image
  → ePaper
```

---

## 9. ePaper 显示

- **文字**：LVGL，`lv_label_set_text(label, text)`，参考 Seeed 官方 XIAO ePaper LVGL 示例（label / panel → 驱动 2.13" ePaper）。
- **图片**：网页已输出 BW bitmap，XIAO 只做 `bitmap → framebuffer → display`，不再做图像处理。

---

## 10. 软件模块划分（最终目录）

```
src/
├── main.c
├── nfc/
│   ├── nfc_manager.c / .h
│   └── ndef_parser.c / .h
├── display/
│   ├── epaper.c / .h
│   ├── welcome_screen.c
│   └── image_renderer.c
├── storage/
│   └── image_storage.c / .h
└── protocol/
    └── xepr_protocol.c / .h
```

> 里程碑 V0.1–V0.3 允许先在现有 `src/main.c` 单文件里实现，跑通后再拆模块。

---

## 11. 状态机

```
BOOT
 ↓
DISPLAY_INIT
 ↓
SHOW_WELCOME
 ↓
NFC_READY
 ↓
WAITING
 ↓
NDEF_UPDATED
 ↓
PARSING
 ↓
VALIDATING
 ↓
DISPLAYING
 ↓
SAVE
 ↓
WAITING
```

异常处理（以下任一情况均回到 `WAITING`，可选择性在屏上闪一条错误提示）：

- CRC ERROR
- Unsupported Format
- Oversize
- Invalid NDEF

---

## 12. 系统全景（Web App ↔ 固件协作）

```
┌─────────────────────────────┐
│          Web App            │
│  Image → Crop → Resize      │
│  → Dither → 1-bit Bitmap    │
│  → XEPR Payload             │
└──────────────┬──────────────┘
               │ NFC Tools / Web NFC
               ↓
┌─────────────────────────────┐
│       XIAO nRF54LM20A       │
│  NFC Writable NDEF          │
│  → NDEF_UPDATED             │
│  → Parse (Text/MIME)        │
│  → Parse XEPR (图片阶段)     │
│  → Bitmap                   │
│  → ePaper Driver            │
└──────────────┬──────────────┘
               ↓
        2.13" ePaper
```

---

## 13. 里程碑（当前开发路线）

### V0.1 —— ePaper 点亮 + 欢迎页

**目标**：启动 ePaper，显示 "Ready for NFC"。

- 集成 SSD1680 驱动（SPI 4 MHz，设备树 overlay 加 DC/RESET/BUSY/CS）。
- LVGL 初始化，创建欢迎页 label。
- 上电显示：

  ```
  NFC ePaper Demo
  Ready for NFC
  Waiting for image...
  ```

- NFC 部分保持现有逻辑不变（可继续写入）。

**验收**：上电 → 墨水屏显示欢迎页；NFC 写入仍正常存 Flash（串口可见 `NDEF message successfully flashed`）。

### V0.2 —— NDEF 文本打印到串口

**目标**：手机写 "Hello"，串口打印 "Hello"。

- 在 worker 线程中调用 NDEF parser，解析 Text Record。
- `NDEF_UPDATED` → 复制 buffer → `k_sem_give` → 解析 → `printk("NDEF text: %s\n", text)`。
- 不涉及 ePaper 刷新。

**验收**：手机 NFC Tools 写入 Text `Hello XIAO`，串口输出该文本；写入非法/非 Text NDEF 时打印错误并回到等待。

### V0.3 —— 文本上屏（第一个完整效果）

**目标**：手机写 "Hello"，ePaper 显示 "Hello"。

- Text 解析结果写入 LVGL label，触发一次完整刷新（ePaper 全刷）。
- 刷屏期间 NFC 仍在线（刷屏在 worker 线程，不阻塞 NFC 回调）。
- （可选加分项）刷屏前先把文本存入 Flash/NVS，重启后恢复显示。

**验收**：手机写 `Hello XIAO` → 墨水屏约 2 秒内刷新并显示 "Hello XIAO"；再写别的文本可再次刷新。

---

## 14. 现有代码资产清单（改造起点）

| 现有内容 | 位置 | 去向 |
|---|---|---|
| NFC T4T 初始化 / emulation | `src/main.c:154-170` | 保留，移入 `nfc_manager.c` |
| `NFC_T4T_EVENT_NDEF_UPDATED` 处理 + 信号量通知 | `src/main.c:90-99, 47-61` | 保留模式，回调改为只 copy + give sem |
| worker 线程（`nfc_write_sem` 等待循环） | `src/main.c:173-185` | 改造为 ndef 处理 worker |
| NDEF file Flash 读写 | `src/ndef_file_m.c` | 保留（`save_image` 后续可复用其 NVS 思路） |
| `CONFIG_NDEF_FILE_SIZE` buffer | `src/main.c:35` | 注意需 ≥ 3904 + header（图片阶段），必要时调大 |
| DK LED/Button 依赖 | `prj.conf`, `main.c` | XIAO 无 DK，逐项移除替换 |
