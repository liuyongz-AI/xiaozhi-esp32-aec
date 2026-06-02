# XiaoZhi ESP32 AEC — 小智 AI 聊天机器人（语音打断增强版）

[![Release](https://img.shields.io/github/v/release/liuyongz-AI/xiaozhi-esp32-aec)](https://github.com/liuyongz-AI/xiaozhi-esp32-aec/releases)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

> **基于 [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) v2.2.4 定制优化**  
> 适配 `bread-compact-wifi-s3cam` 板型（ST7789 1.54寸 LCD + OV3660 摄像头）  
> ? 已启用服务端 AEC 语音打断功能

---

## ?? 目录

- [特色功能](#-特色功能)
- [硬件配置](#-硬件配置)
- [快速开始（固件烧录）](#-快速开始固件烧录)
- [本地编译](#-本地编译)
- [版本记录](#-版本记录)
- [与上游差异](#-与上游差异)
- [常见问题](#-常见问题)

---

## ?? 特色功能

### 核心能力
- **语音打断（AEC）** — 服务端回声消除，AI 说话时可直接中断对话
- **语音唤醒** — 离线唤醒词检测（基于 ESP-SR）
- **流式语音交互** — ASR + LLM + TTS 全双工对话架构
- **摄像头视觉** — OV3660 摄像头支持，AI 可"看见"画面
- **MCP 协议控制** — 云端 MCP 扩展大模型能力（智能家居、桌面操作等）

### 通信协议
- WebSocket 协议
- MQTT + UDP 混合协议

### 显示与交互
- ST7789 1.54寸 240x240 LCD 显示
- 表情动画与 UI 界面
- BOOT 按键交互（单击切换对话 / 长按进入配网）

---

## ?? 硬件配置

### 适用板型：`bread-compact-wifi-s3cam`

| 组件 | 型号 |
|------|------|
| 主控 | ESP32-S3（16MB Flash） |
| 屏幕 | LCD 1.54寸 ST7789（240x240） |
| 摄像头 | OV3660 |
| 麦克风 | I2S 数字麦克风 |
| 扬声器 | I2S 数字功放 |

### GPIO 引脚定义

**显示屏（ST7789）**

| 功能 | GPIO |
|------|------|
| 背光 | GPIO_NUM_38 |
| MOSI | GPIO_NUM_20 |
| CLK  | GPIO_NUM_19 |
| DC   | GPIO_NUM_47 |
| RST  | GPIO_NUM_21 |
| CS   | GPIO_NUM_45 |

**音频（I2S）**

| 功能 | 引脚 |
|------|------|
| MIC WS   | GPIO_NUM_1 |
| MIC SCK  | GPIO_NUM_2 |
| MIC DIN  | GPIO_NUM_42 |
| SPK DOUT | GPIO_NUM_39 |
| SPK BCLK | GPIO_NUM_40 |
| SPK LRCK | GPIO_NUM_41 |

**摄像头（OV3660）**

| 功能 | 引脚 | 功能 | 引脚 |
|------|------|------|------|
| D0  | 11 | D1  | 9 |
| D2  | 8  | D3  | 10 |
| D4  | 12 | D5  | 18 |
| D6  | 17 | D7  | 16 |
| XCLK | 15 | PCLK | 13 |
| VSYNC | 6 | HREF | 7 |
| SIOC | 5 | SIOD | 4 |

**其他**

| 功能 | GPIO |
|------|------|
| 内置 LED | GPIO_NUM_48 |
| BOOT 按钮 | GPIO_NUM_0 |
| MCP 灯控 | GPIO_NUM_14 |

---

## ?? 快速开始（固件烧录）

### 方法一：下载预编译固件（推荐）

从 [Releases](https://github.com/liuyongz-AI/xiaozhi-esp32-aec/releases) 页面下载最新版的 `xiaozhi-merged.bin`。

### 方法二：使用 esptool 烧录

```bash
# 安装 esptool
pip install esptool

# 擦除 flash
esptool.py --chip esp32s3 -b 460800 erase_flash

# 烧录合并固件（从 0x0 地址）
esptool.py --chip esp32s3 -b 460800 write_flash \
  --flash_mode dio --flash_size 16MB --flash_freq 80m \
  0x0 xiaozhi-merged.bin
```

> **注意**：`xiaozhi-merged.bin` 是合并后的完整固件，包含 bootloader、分区表、应用固件和资源文件，从 `0x0` 地址烧录即可，无需分段烧录。

### 首次使用

1. 烧录完成后上电，设备会进入 WiFi 配网模式
2. 使用手机连接热点 `Xiaozhi-xxxx`，打开浏览器访问 `192.168.4.1`
3. 配置 WiFi 网络
4. 设备自动连接服务器 [xiaozhi.me](https://xiaozhi.me)，注册账号即可使用

---

## ?? 本地编译

### 环境要求

- ESP-IDF v5.5.4 或更新版本
- Python 3.12+
- CMake、Ninja 构建工具

### 编译步骤

```bash
# 1. 设置目标芯片
idf.py set-target esp32s3

# 2. 配置板型（选择 bread-compact-wifi-s3cam）
idf.py menuconfig
# 进入 Xiaozhi Assistant → Board Type → 选择 Bread Compact WiFi + LCD + Camera

# 3. 编译
idf.py build

# 4. 合并固件
idf.py merge-bin

# 或手动合并：
python -m esptool --chip esp32s3 merge_bin \
  --output build/xiaozhi-merged.bin \
  --flash_mode dio --flash_size 16MB --flash_freq 80m \
  0x0 build/bootloader/bootloader.bin \
  0x8000 build/partition_table/partition-table.bin \
  0xd000 build/ota_data_initial.bin \
  0x20000 build/xiaozhi.bin \
  0x800000 build/generated_assets.bin
```

### AEC 相关配置

```bash
idf.py menuconfig
# Xiaozhi Assistant → Enable Server-Side AEC (Unstable)  →  启用
```

或者在 `sdkconfig.defaults` 中添加：

```
CONFIG_USE_AUDIO_PROCESSOR=y
CONFIG_USE_SERVER_AEC=y
```

---

## ?? 版本记录

### v2.2.4-aec（当前最新）

**发布日期**：2026-06-02

| 变更 | 说明 |
|------|------|
| ?? **AEC 语音打断** | 启用服务端回声消除，AI 说话时可语音打断 |
| ?? **板型适配** | 新增 `bread-compact-wifi-s3cam` 配置（ST7789 + OV3660） |
| ?? **移除冗余功耗切换** | 连接状态不再反复切换功耗模式 |
| ?? **唤醒提示音** | 唤醒词检测后播放提示音，交互反馈更清晰 |
| ? **性能优化** | MCP 消息传递改用移动语义，减少字符串拷贝 |
| ?? **代码清理** | 移除未使用的 MCP 广播回调接口 |

### v2.2.4（上游基础版本）

基于 [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) 的原始 v2.2.4 版本。

---

## ?? 与上游差异

本项目基于 [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) v2.2.4 定制，主要差异：

| 对比项 | 上游版本 | 本分支 |
|--------|----------|--------|
| AEC 语音打断 | ? 默认关闭 | ? 默认启用（`CONFIG_USE_SERVER_AEC=y`） |
| bread-compact-wifi-s3cam 板型 | ? 无默认配置 | ? `sdkconfig.defaults.bread-compact-wifi-s3cam` |
| 功耗管理 | 连接时切换高性能模式 | ? 移除冗余切换 |
| 唤醒反馈 | 无声 | ? 播放提示音 |
| MCP 消息 | 值拷贝 | ? 移动语义优化 |

---

## ? 常见问题

### Q: 刷机后屏幕黑屏？
- 确认烧录地址为 `0x0`，并使用合并后的 `xiaozhi-merged.bin`
- 检查 GPIO 引脚定义是否与你的硬件接线一致（见上方引脚表）
- 确认背光供电正常

### Q: 语音打断不生效？
- 确认服务器端支持 AEC（服务端 AEC 需要服务器配合）
- 检查 `CONFIG_USE_AUDIO_PROCESSOR=y` 和 `CONFIG_USE_SERVER_AEC=y` 已启用
- 如果使用自定义服务器，需确保服务器实现了 AEC 逻辑

### Q: 如何切换回上游版本？
- 从 [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) 下载官方固件重新烧录即可

### Q: 支持哪些服务器？
- 默认接入 [xiaozhi.me](https://xiaozhi.me) 官方服务器
- 也支持自建服务器（参考下游开源服务器项目）

---

## ?? 许可证

本项目基于 [MIT 许可证](LICENSE) 发布，代码源自 [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32)。

上游项目由虾哥（78）开源，允许任何人免费使用、修改或用于商业用途。

---

## ? Star History

[![Star History Chart](https://api.star-history.com/svg?repos=liuyongz-AI/xiaozhi-esp32-aec&type=Date)](https://star-history.com/#liuyongz-AI/xiaozhi-esp32-aec&Date)
