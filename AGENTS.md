# Retro-Go 项目上下文指南

> 本文件为 AI 代理（如 iFlow CLI）提供项目上下文信息，以便更好地理解和操作此代码库。

---

## 项目概述

**Retro-Go** 是一个为 ESP32 系列设备设计的复古游戏固件，支持多种经典游戏主机的模拟。该项目由一个启动器（launcher）和多个模拟器应用程序组成，经过高度优化以适应嵌入式设备的 CPU、内存和闪存限制。

### 支持的系统
- **任天堂**：NES、SNES、Gameboy、Gameboy Color、Game Boy Advance、Game & Watch
- **世嘉**：SG-1000、Master System、Mega Drive / Genesis、Game Gear
- **Coleco**：Colecovision
- **NEC**：PC Engine
- **雅达利**：Lynx
- **微软**：MSX

### 主要特性
- 游戏中菜单、收藏和最近游玩
- GB 色彩调色板、RTC 调整和保存
- NES 色彩调色板、PAL 游戏、NSF 支持
- 缩放和过滤选项
- 涡轮速度/快进
- 可定制的启动器、封面艺术和存档预览
- 每个游戏多个存档槽
- Wifi 文件管理器

### 当前目标平台
- **ESP32-P4** (RISC-V 双核 360MHz) - 主要开发目标
- 其他 ESP32 变体（S2、S3 等）

---

## 目录结构

```
retro-go-1.4x-gbaback/
├── components/retro-go/     # 核心库（显示、音频、输入、存储等）
│   ├── targets/             # 硬件目标配置
│   │   └── esp32p4/         # ESP32-P4 目标配置
│   ├── drivers/             # 硬件驱动
│   ├── fonts/               # 字体文件
│   └── rg_*.c/h             # 核心模块
├── launcher/                # 启动器应用
├── retro-core/              # 多合一模拟器核心（NES, GB/GBC, SNES, PCE, G&W, Lynx, SMS/GG/COL）
├── gbsp/                    # Game Boy Advance 模拟器（带 JIT）
├── gwenesis/                # Genesis/Mega Drive 模拟器
├── fmsx/                    # MSX 模拟器
├── prboom-go/               # DOOM 引擎移植
├── themes/                  # 主题资源
├── assets/                  # 静态资源
├── rg_tool.py               # 构建工具（主入口）
└── base.cmake               # CMake 配置
```

---

## 构建系统

### 前提条件
- **ESP32-P4 目标**：需要安装 **ESP-IDF 5.5** 或更高版本
- **其他 ESP32 目标**：需要安装 **ESP-IDF 4.4 到 5.3** 版本
- 可能需要应用 `tools/patches` 中的补丁（某些设备）
- 环境变量 `IDF_PATH` 必须正确设置

### 主要构建命令

使用 `rg_tool.py` 作为主要构建入口（**不要直接使用 `idf.py`**）：

```bash
# 查看帮助
python rg_tool.py --help

# 构建固件镜像（.img 文件，用于串口刷写）
python rg_tool.py build-img --target esp32p4

# 构建固件文件（.fw 文件，用于 SD 卡安装）
python rg_tool.py build-fw --target esp32p4

# 清洁构建（发布版本）
python rg_tool.py release --target esp32p4

# 构建特定应用
python rg_tool.py build --target esp32p4 launcher retro-core

# 清理构建
python rg_tool.py clean

# 带性能分析的构建
python rg_tool.py profile
```

### ESP32-P4 网络配置
ESP32-P4 默认禁用网络，如需启用：

```bash
# 默认构建（禁用网络，推荐）
python rg_tool.py build --target esp32p4

# 启用网络支持
python rg_tool.py build --target esp32p4 --with-networking
```

### 开发模式命令

首次需刷写完整镜像后，可单独刷写应用以加快开发：

```bash
# 刷写单个应用到设备
python rg_tool.py --port=COM3 flash retro-core

# 监控串口输出
python rg_tool.py --port=COM3 monitor retro-core

# 刷写并监控
python rg_tool.py --port=COM3 run retro-core

# 安装完整镜像到设备
python rg_tool.py --target esp32p4 --port=COM3 install
```

### Windows 注意事项
- 必须使用 `python rg_tool.py ...` 而非 `./rg_tool.py ...`
- 默认串口为 `COM3`，可通过 `--port` 参数或 `RG_TOOL_PORT` 环境变量修改
- 确保在 ESP-IDF 环境中运行（运行 `export.bat`）

---

## 应用程序说明

| 应用名称 | 描述 | 默认启用 |
|---------|------|---------|
| `launcher` | 启动器/主菜单（必需） | ✅ |
| `retro-core` | 多合一模拟器（NES, SNES, GB/GBC, PCE, G&W, Lynx, SMS/GG/COL） | ✅ |
| `gbsp` | Game Boy Advance 模拟器（带 JIT 支持） | ✅ |
| `gwenesis` | Genesis/Mega Drive 模拟器 | ❌ |
| `fmsx` | MSX 模拟器 | ❌ |
| `prboom-go` | DOOM 引擎 | ❌ |

> 注意：`retro-core` 包含多个模拟器（NES、SNES、GB/GBC、PCE、G&W、Lynx 和 SMS/GG/COL），它们不能单独选择。

---

## 硬件配置（ESP32-P4）

### 主要硬件参数
- **芯片**：ESP32-P4 (RISC-V 双核 360MHz)
- **存储**：SDMMC 4位模式 (GPIO 39-44, LDO Channel 4)
- **显示**：ST7789V SPI 320x240 (GPIO 28-33) 或 ST7701 MIPI DSI（可选）
- **输入**：ADC 方向键 + GPIO 按键 (GPIO 8, 11-14, 35)
- **音频**：可选外部 I2S DAC (GPIO 46-49)
- **性能**：支持双核并行优化、L2 缓存、PPA 硬件加速

### 核心亲和性配置
- Core 0: 模拟器主循环、输入处理、系统监控
- Core 1: 显示、SPI、音频（I/O 密集型）

### 目标配置文件
- `components/retro-go/targets/esp32p4/config.h` - 硬件引脚和功能配置
- `components/retro-go/targets/esp32p4/sdkconfig` - ESP-IDF 配置
- `components/retro-go/targets/esp32p4/env.py` - 构建环境变量

---

## 核心模块

| 模块 | 文件 | 功能 |
|-----|------|-----|
| 显示 | `rg_display.c/h` | 屏幕驱动、帧缓冲管理 |
| 音频 | `rg_audio.c/h` | 音频输出、DAC 控制 |
| 输入 | `rg_input.c/h` | 按键处理、手柄输入 |
| 存储 | `rg_storage.c/h` | SD 卡、文件系统 |
| GUI | `rg_gui.c/h` | 用户界面组件 |
| 系统 | `rg_system.c/h` | 系统初始化、电源管理 |
| 设置 | `rg_settings.c/h` | 配置持久化 |
| 网络 | `rg_network.c/h` | Wifi、HTTP 服务 |
| 本地化 | `rg_localization.c/h` | 多语言支持 |

---

## 使用配置

### 游戏封面/艺术
游戏封面应放在 SD 卡根目录的 `romart` 文件夹中。

命名方案：
- 基于文件名：`/romart/nes/Super Mario.png`（不包含 rom 扩展名）
- 基于 CRC32：`/romart/nes/A/ABCDE123.png`（`A` 是 CRC32 的第一个字符）

> 注意：基于文件名的格式响应速度更快，推荐使用。

### BIOS 文件路径
一些模拟器支持加载 BIOS，文件应放置如下：
- GB：`/retro-go/bios/gb_bios.bin`
- GBC：`/retro-go/bios/gbc_bios.bin`
- FDS：`/retro-go/bios/fds_bios.bin`
- MSX：`/retro-go/bios/msx/` 目录下放置：`MSX.ROM`、`MSX2.ROM`、`MSX2EXT.ROM`、`MSX2P.ROM`、`MSX2PEXT.ROM`、`FMPAC.ROM`、`DISK.ROM`、`MSXDOS2.ROM`、`PAINTER.ROM`、`KANJI.ROM`

### Game & Watch
ROM 必须用 [LCD-Game-Shrinker](https://github.com/bzhxx/LCD-Game-Shrinker) 打包。

### Wifi 配置
创建 `/retro-go/config/wifi.json` 配置文件，最多定义 4 个网络：

```json
{
  "ssid0": "my-network",
  "password0": "my-password",
  "ssid1": "my-other-network",
  "password1": "my-password"
}
```

时间同步通过 NTP（`pool.ntp.org`）完成，时区在启动器选项菜单中配置。文件管理器通过 `http://192.168.x.x/` 访问。

### 外部 DAC（耳机）
支持 ODROID-GO 的外部 DAC 模块，在菜单中选择 `Audio Out: Ext DAC`。

---

## 开发规范

### 代码风格
- C99 标准（C++03 用于 handy-go）
- 使用 `.clang-format` 进行代码格式化
- 注释应解释"为什么"而非"做什么"

### 目标特定代码
使用预处理器宏为特定目标添加代码：

```c
#ifdef RG_TARGET_ESP32P4
    // ESP32-P4 特定代码
#endif
```

### 本地化
字符串应使用 `_(...)` 宏包装以支持翻译：

```c
rg_gui_alert(_("Error"), _("File not found"));
```

### 添加新模拟器
1. 在项目根目录创建新文件夹
2. 包含 `CMakeLists.txt`、`main/main.c` 和 `components/` 目录
3. 在 `rg_tool.py` 的 `PROJECT_APPS` 中添加配置
4. 参考现有模拟器的结构

### 更新主题图像
1. 编辑 `themes/default/` 中的图像文件
2. 运行 `tools/gen_images.py` 重新生成 `launcher/main/images.c`

---

## 常见任务

### 修改显示驱动
1. 编辑 `components/retro-go/targets/esp32p4/config.h`
2. 修改 `RG_SCREEN_INIT()` 宏中的初始化序列
3. 运行 `python rg_tool.py clean` 后重新构建

### 添加新目标设备
1. 复制 `components/retro-go/targets/esp32p4/` 到新文件夹
2. 修改 `config.h` 中的引脚配置
3. 使用 `idf.py menuconfig` 生成 `sdkconfig`
4. 创建 `env.py` 设置环境变量

---

## 调试与故障排除

### 崩溃日志
崩溃信息会保存到 `/sd/crash.log`（需要应用 ESP-IDF 补丁）。

解析回溯：
```bash
xtensa-esp32-elf-addr2line -ifCe app-name/build/app-name.elf
```

### 启动循环恢复
按住 `DOWN` 键开机可返回启动器。

### 常见问题
- **构建失败**: 确保在 ESP-IDF 环境中运行（运行 `export.bat`）
- **目标不匹配**: 删除 `app/sdkconfig` 后重新构建
- **闪存失败**: 检查串口连接和波特率设置
- **黑屏/启动循环**: 按住 `DOWN` 键开机返回启动器
- **ZIP 文件**: ZIP 归档文件应仅包含一个 ROM 文件

### Game Boy SRAM 说明
- 存档提供最佳的保存体验
- SRAM 格式与 VisualBoyAdvance 兼容
- 恢复游戏时，如存在存档会优先使用存档

---

## 许可证

- 主项目：GPLv2
- 例外：
  - `fmsx/components/fmsx`（MSX 模拟器，自定义非商业许可证）
  - `handy-go/components/handy`（Lynx 模拟器，zlib）

---

## 相关文档

- [README.md](README.md) - 项目介绍和安装指南
- [BUILDING.md](BUILDING.md) - 详细构建说明
- [docs/BRINGUP.md](docs/BRINGUP.md) - 实机验证清单与实测数据（韩文）
- [docs/ROADMAP.md](docs/ROADMAP.md) - 性能与设计路线图（韩文）
- [docs/FLASHING.md](docs/FLASHING.md) - 烧录、接线、SD 卡布局、恢复
- [docs/upstream/项目说明.md](docs/upstream/项目说明.md) - 中文详细说明（包含 ESP32-P4 特定内容）