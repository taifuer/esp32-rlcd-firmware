# ESP32 RLCD Firmware

`ESP32 RLCD Firmware` 是面向微雪 ESP32-S3-RLCD-4.2 开发板的原生 ESP-IDF 固件。
项目目前从稳定的离线时钟和板载外设支持起步，再逐步扩展联网校时、天气等信息展示
能力。

当前版本为 **v0.1.1**，已在 16 MB Flash、8 MB PSRAM 的实机上完成构建、烧录和
运行验证。

## 已实现

- 在 400 × 300 ST7305 全反射屏上显示时间、日期、星期、温度和湿度；
- 读取 PCF85063 RTC，并检查时间数据是否有效；
- 读取 SHTC3 温湿度及芯片 ID，包含 CRC 校验；
- 通过 USB Serial/JTAG 提供 `SET_TIME`、`GET_TIME` 和 `HELP` 命令；
- 从 Windows 获取上海时间并写入 RTC，写入后自动回读确认；
- 初始化并自检 8 MB Octal PSRAM；
- 在屏幕底部显示当前阶段的诊断文本 `Hello, world.`。

v0.1.1 完全离线运行，尚未启用 Wi-Fi、NTP、语音和 OTA。固件只会在收到明确的
`SET_TIME YYYY-MM-DD HH:MM:SS` 命令后修改 RTC。

## 硬件与开发环境

| 项目 | 当前配置 |
| --- | --- |
| 开发板 | Waveshare ESP32-S3-RLCD-4.2 |
| 显示屏 | 4.2 英寸、400 × 300、ST7305 全反射屏 |
| 板载器件 | PCF85063 RTC、SHTC3 温湿度传感器 |
| 存储 | 16 MB Flash、8 MB Octal PSRAM |
| SDK | ESP-IDF v5.5.3 |
| 开发方式 | WSL 负责编译，Windows COM 口负责烧录和串口校时 |

下文使用 `COM5` 作为示例；如果设备管理器显示的是其他端口，请替换为实际端口号。

## 快速开始

### 1. 准备依赖

首次使用时执行：

```bash
./scripts/bootstrap.sh
```

脚本会在仓库外安装固定版本的 ESP-IDF 和显示组件。已有环境可以只做完整性检查：

```bash
./scripts/bootstrap.sh --check
```

依赖版本固定在 [`tool-versions.env`](tool-versions.env)，目录选择和升级规则见
[`docs/development.md`](docs/development.md)。

### 2. 构建

```bash
./scripts/build.sh
```

主要产物位于 `build/`：

- `rlcd_firmware.bin`：应用镜像，不能烧录到 `0x0`；
- `rlcd_firmware_merged.bin`：包含 Bootloader、分区表和应用的完整镜像；
- `SHA256SUMS`：本次构建产物的校验值。

构建和烧录是两个独立步骤；运行构建脚本不会连接或改写开发板。

### 3. 进入下载模式

保持 Type-C 连接，然后：

1. 长按 `PWR` 关机；
2. 按住 `BOOT`；
3. 短按 `PWR` 开机；
4. 等待约 2 秒后松开 `BOOT`。

此时开发板进入 ESP32-S3 ROM 下载模式，与 PC 的 BIOS 无关。

### 4. 烧录

烧录刚完成的本地构建：

```bash
./scripts/flash.sh --port COM5 --confirm
```

也可以直接烧录仓库中已经实机验证的 v0.1.1 完整固件：

```bash
./scripts/flash.sh --port COM5 \
  --firmware dist/v0.1.1/esp32-rlcd-firmware-v0.1.1.bin \
  --confirm
```

烧录脚本会检查固件哈希、目标 USB 设备和芯片型号，再将完整镜像写入地址 `0x0`。
详细过程和故障排查见 [`docs/flashing.md`](docs/flashing.md)。

烧录完成后，长按 `PWR` 关机；不要按 `BOOT`，短按 `PWR` 正常启动。

### 5. 设置 RTC

开发板正常启动后执行：

```bash
./scripts/set-rtc.sh --port COM5
```

脚本会把当前上海时间发送给开发板。终端出现 `RTC_SET_OK`，并且随后读到的秒数持续
递增，即表示校时成功。

## 项目结构

```text
.
├── src/                     # 全部自研固件源码
│   ├── app/                 # 应用入口与 USB 串口校时
│   ├── board/               # 板级总线和引脚定义
│   ├── display/             # ST7305 显示界面
│   ├── rtc/                 # PCF85063 RTC 驱动
│   └── sensors/             # SHTC3 传感器驱动
├── dist/                    # 按版本保存的已验证完整固件
├── scripts/                 # 依赖准备、构建、烧录和校时脚本
├── docs/                    # 开发、烧录与实机验证记录
├── CHANGELOG.md
├── CMakeLists.txt
├── sdkconfig.defaults
└── tool-versions.env
```

构建目录、机器相关配置和外部依赖不纳入版本控制；第三方组件的来源与许可说明见
[`NOTICE.md`](NOTICE.md)。

## 文档

- [烧录流程与故障排查](docs/flashing.md)
- [开发、依赖与发布流程](docs/development.md)
- [实机验证记录](docs/bringup-log.md)
- [版本变更记录](CHANGELOG.md)
- [v0.1.1 固件说明与校验值](dist/v0.1.1/README.md)

## 下一步

v0.2 计划加入 Wi-Fi 配网和 NTP 校时，统一使用上海时区，并在断网时继续使用
PCF85063 RTC 保持时间显示。
