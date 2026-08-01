# Retro-Go ESP32-P4

基于 Retro-Go GBA 开发版本的修改版，针对 ESP32-P4 平台进行深度优化和功能扩展。

## 目录

- [项目简介](#项目简介)
- [安装指南](#安装指南)
- [使用说明](#使用说明)
- [常见问题](#常见问题)
- [开发进度](#开发进度)
- [开发指南](#开发指南)
- [致谢](#致谢)
- [许可证](#许可证)

---

## 项目简介

本项目是基于 **Retro-Go** 项目 GBA 开发版本的修改版，针对 ESP32-P4 平台进行了深度优化和功能扩展。

Retro-Go 原项目是一个在基于 ESP32 的设备上运行复古游戏的固件，由一个启动器和多个模拟器应用程序组成，经过大量优化以减少 CPU、内存和闪存需求。

### 本版本改进

- ✅ GBA 模拟器 JIT 执行框架集成
- ✅ 系统性能优化，提高流畅度
- ✅ SNES S-DD1 芯片支持
- ⚠️ SNES SuperFX 芯片支持（代码已添加，画面异常待修复）
- ✅ 中文显示和字库支持

### 支持的系统

| 厂商 | 支持平台 |
|------|---------|
| 任天堂 | NES、SNES、Gameboy、Gameboy Color、Game Boy Advance、Game & Watch |
| 世嘉 | SG-1000、Master System、Mega Drive / Genesis、Game Gear |
| Coleco | Colecovision |
| NEC | PC Engine |
| 雅达利 | Lynx |
| 微软 | MSX |

### 主要功能

| 类别 | 功能 |
|------|------|
| 游戏体验 | 游戏中菜单、涡轮速度/快进、缩放和过滤选项 |
| 存档系统 | 收藏和最近游玩、每个游戏多个存档槽、封面艺术和存档预览 |
| 平台特性 | GB 色彩调色板、NES 色彩调色板、PAL 游戏支持、NSF 支持 |
| 网络功能 | Wifi 文件管理器、RTC 调整和保存 |

---

## 安装指南

### ESP32-P4 硬件规格

| 组件 | 规格 |
|------|------|
| 芯片 | ESP32-P4 (RISC-V 双核 360MHz) |
| 存储 | SDMMC 4位模式 (GPIO 39-44, LDO Channel 4) |
| 显示 | ST7789V SPI 320x240 (GPIO 28-33) 或 ST7701 MIPI DSI (可选) |
| 输入 | ADC 方向键 + GPIO 按键 (GPIO 8, 11-14, 35) |
| 音频 | 可选外部 I2S DAC (GPIO 46-49) |
| 性能 | 双核并行优化、L2 缓存、PPA 硬件加速 |

### 安装步骤

1. 下载适用于 ESP32-P4 的 `.img` 文件
2. 用 USB 电缆将设备连接到计算机
3. 用 esptool 刷新图像：

   ```bash
   # 命令行方式
   esptool.py write_flash --flash_size detect 0x0 retro-go_*.img
   ```

   或访问 [esptool-js](https://espressif.github.io/esptool-js/) 使用网页版工具。

### 构建说明

```bash
# 默认构建（禁用网络，推荐）
python rg_tool.py build --target esp32p4

# 启用网络构建
python rg_tool.py build --target esp32p4 --with-networking
```

---

## 使用说明

### 游戏封面

游戏封面放在 SD 卡 `romart` 文件夹中。

| 命名方式 | 格式 | 示例 |
|----------|------|------|
| 文件名 | `/romart/<平台>/<游戏名>.png` | `/romart/nes/Super Mario.png` |
| CRC32 | `/romart/<平台>/<首字符>/<CRC32>.png` | `/romart/nes/A/ABCDE123.png` |

> 💡 基于文件名的格式响应更快，推荐使用。

### BIOS 文件

| 平台 | 路径 |
|------|------|
| GB | `/retro-go/bios/gb_bios.bin` |
| GBC | `/retro-go/bios/gbc_bios.bin` |
| FDS | `/retro-go/bios/fds_bios.bin` |
| MSX | `/retro-go/bios/msx/` |

MSX 所需文件：`MSX.ROM`、`MSX2.ROM`、`MSX2EXT.ROM`、`MSX2P.ROM`、`MSX2PEXT.ROM`、`FMPAC.ROM`、`DISK.ROM`、`MSXDOS2.ROM`、`PAINTER.ROM`、`KANJI.ROM`

### Game & Watch

ROM 必须用 [LCD-Game-Shrinker](https://github.com/bzhxx/LCD-Game-Shrinker) 打包。

教程：[LCD-Game-Shrinker 使用指南](https://gist.github.com/DNA64/16fed499d6bd4664b78b4c0a9638e4ef)

### Wifi 配置

创建 `/retro-go/config/wifi.json`（最多 4 个网络）：

```json
{
  "ssid0": "network-name",
  "password0": "password"
}
```

**功能：**
- 时间同步：通过 NTP (`pool.ntp.org`) 自动同步
- 文件管理器：访问 `http://192.168.x.x/`（IP 在设备"关于"菜单查看）

### 外部 DAC

支持 [ODROID-GO 外部 DAC 模块](https://github.com/backofficeshow/odroid-go-audio-hat)，菜单中选择 `Audio Out: Ext DAC`。

<details>
<summary>引脚定义</summary>

| GO PIN | PCM5102A PIN |
|--------|-------------|
| 1 | GND |
| 3 | LCK |
| 4 | DIN |
| 5 | BCK |
| 6 | VIN |

</details>

---

## 常见问题

### 黑屏 / 启动循环

按住 `DOWN` 键开机返回启动器。

### 音质问题

| 方案 | 说明 |
|------|------|
| 快速方案 | 串联 33Ω 电阻器 |
| 完整方案 | [ODROID-GO Silent Volume](https://wiki.odroid.com/odroid_go/silent_volume) |
| 最佳方案 | 使用外部 DAC 模块 |

### Game Boy SRAM

- 存档提供最佳保存体验
- SRAM 格式与 VisualBoyAdvance 兼容
- 恢复游戏时优先使用存档

### ZIP 文件

- ZIP 归档应仅包含一个 ROM 文件
- 大型 ROM 可能因内存限制加载失败

---

## 开发进度

### GBA 模拟器 (gbsp)

| 功能 | 状态 | 说明 |
|------|------|------|
| JIT 框架 | ✅ 已完成 | 位于 `gbsp/components/jit_dev/` |
| JIT 执行 | ⚠️ 进行中 | 目前解释器更流畅，`jit_enabled = false` |
| 解释器模式 | ✅ 已启用 | 当前默认使用解释器执行全部指令 |

<details>
<summary>JIT 配置说明</summary>

通过 `gbsp/main/main.c` 中的 `static bool jit_enabled` 控制：

| 值 | 执行模式 |
|----|----------|
| `true` | 简单指令由 JIT 执行，其余由解释器 |
| `false` | 全部由解释器执行（当前推荐，更流畅） |

</details>

### 系统优化

| 功能 | 状态 |
|------|------|
| Retro-Go 系统优化 | ✅ 提高了流畅度 |

### NES 模拟器

| 功能 | 状态 |
|------|------|
| 游戏兼容性 | ✅ 添加了更多游戏支持 |

### SFC/SNES 模拟器

| 功能 | 状态 | 说明 |
|------|------|------|
| S-DD1 芯片 | ✅ 已完成 | 已添加芯片支持 |
| SuperFX 芯片 | ⚠️ 进行中 | 代码已添加，画面显示异常待修复 |

### 本地化

| 功能 | 状态 |
|------|------|
| 中文显示 | ✅ 已完成 |
| 中文字库 | ✅ 已完成 |

---

## 开发指南

### 前提条件

| 目标平台 | ESP-IDF 版本 |
|----------|-------------|
| ESP32-P4 | 5.5 或更高 |
| 其他 ESP32 | 4.4 ~ 5.3 |

### 构建命令

#### 完整固件

```bash
python rg_tool.py build-img --target esp32p4    # .img 文件（串口刷写）
python rg_tool.py build-fw --target esp32p4    # .fw 文件（SD 卡安装）
python rg_tool.py release --target esp32p4     # 清洁构建（发布版）
```

#### 特定应用

```bash
python rg_tool.py build --target esp32p4 launcher retro-core
```

> `retro-core` 包含：NES、SNES、GB/GBC、PCE、G&W、Lynx、SMS/GG/COL（不可单独选择）

#### 开发模式

```bash
python rg_tool.py --port=COM3 flash retro-core    # 刷写单个应用
python rg_tool.py --port=COM3 monitor retro-core  # 监控输出
python rg_tool.py --port=COM3 run retro-core      # 刷写并监控
```

#### 其他命令

```bash
python rg_tool.py --help      # 查看帮助
python rg_tool.py clean       # 清理构建
python rg_tool.py profile     # 性能分析构建
python rg_tool.py install     # 安装镜像到设备
```

### Windows 注意事项

1. 使用 `python rg_tool.py ...` 而非 `./rg_tool.py ...`
2. 确保在 ESP-IDF 环境中运行（执行 `export.bat`）
3. 默认串口 `COM3`，可通过 `--port` 修改

### 其他说明

| 主题 | 位置 |
|------|------|
| 主题定制 | 图像位于 `themes/default/`，修改后运行 `python tools/gen_images.py` |
| 移植指南 | 详见 `components/retro-go/README.md` |
| 翻译 | 字符串使用 `_(...)` 宏包装，如 `_("Error")` |

---

## 致谢

| 组件 | 来源 |
|------|------|
| NES/GBC/SMS 模拟器 | [Go-Play firmware](https://github.com/othercrashoverride/go-play) |
| 启动器设计 | [pelle7's go-emu](https://github.com/pelle7/odroid-go-emu-launcher) |
| PCE-GO | [HuExpress](https://github.com/kallisti5/huexpress) |
| Lynx 模拟器 | [libretro-handy](https://github.com/libretro/libretro-handy) |
| SNES 模拟器 | [Snes9x 2005](https://github.com/libretro/snes9x2005) |
| Genesis 模拟器 | [Gwenesis](https://github.com/bzhxx/gwenesis/) |
| G&W 模拟器 | [lcd-game-emulator](https://github.com/bzhxx/lcd-game-emulator) |
| MSX 模拟器 | [fMSX](https://fms.komkon.org/fMSX/) |
| PNG 支持 | [lodepng](https://github.com/lvandeve/lodepng/) |

特别感谢：[RGHandhelds](https://www.rghandhelds.com/)、[MyRetroGamecase](https://www.myretrogamecase.com/)、[ODROID-GO 社区](https://forum.odroid.com/viewtopic.php?f=159&t=37599)

---

## 许可证

| 组件 | 许可证 |
|------|--------|
| 主项目 | GPLv2 |
| fmsx/components/fmsx | 自定义非商业许可证 |
| handy-go/components/handy | zlib |
