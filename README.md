# ESP32 RLCD Firmware

`ESP32 RLCD Firmware` 是面向微雪 ESP32-S3-RLCD-4.2 开发板的原生 ESP-IDF 固件。
项目从可靠的离线时钟和板载外设支持起步，逐步扩展联网校时、天气等信息展示能力。

> 本项目是独立的社区固件，与 Espressif Systems、Waveshare 及相关上游项目不存在隶属、
> 赞助或官方背书关系。产品名和商标仅用于准确说明兼容硬件与依赖来源。

![ESP32 RLCD Firmware 首屏布局示意图](docs/assets/home-screen.svg)

> v0.2.0 首屏示意图，按当前 400 × 300 布局和实机数据绘制；实物为单色全反射屏，
> 观感会随环境光变化。

## 项目状态

| 项目 | 状态 |
| --- | --- |
| 当前源码 | **v0.2.0**：主机测试、完整构建和实机功能/布局验收均已通过 |
| 最新正式固件 | **v0.2.0**：从 [GitHub Releases](https://github.com/taifuer/esp32-rlcd-firmware/releases/latest) 下载；仓库内文件见 [`dist/v0.2.0/`](dist/v0.2.0/) |
| 当前运行方式 | 完全离线；Wi-Fi、NTP、语音和 OTA 尚未启用 |

## 已实现

- 在 400 × 300 ST7305 全反射屏上等大显示 `HH:MM:SS`，并显示公历、农历、中文星期、
  温湿度和电池电量；
- 离线换算 2000—2099 年农历日期，包含闰月；
- 读取 PCF85063 RTC，并检查时间数据是否有效；
- 读取 SHTC3 温湿度及芯片 ID，包含 CRC 校验；
- 通过 GPIO4 ADC 读取 18650 电池电压，经过三倍分压还原、滤波和电量曲线换算；
- 通过 USB Serial/JTAG 提供 `SET_TIME`、`GET_TIME` 和 `HELP` 命令；
- 从 Windows 获取上海时间并写入 RTC，写入后自动回读确认；
- 初始化并自检 8 MB Octal PSRAM。

固件只会在收到明确的 `SET_TIME YYYY-MM-DD HH:MM:SS` 命令后修改 RTC。

## 安装发布固件

普通用户从 GitHub Releases 或 `dist/` 获取完整合并固件即可，不需要安装 ESP-IDF，也
不需要自行编译。GitHub Releases 只保留最新正式版本，历史版本继续保存在 `dist/`。
合并固件包含 Bootloader、分区表和应用，统一从地址 `0x0` 写入；它不包含或改写芯片内置
ROM、eFuse，也不存在 PC 主板式的 BIOS。

Release 同时提供项目许可证、第三方声明和完整许可文本压缩包。只需安装固件的用户主要
使用 `.bin` 与 `SHA256SUMS`；再分发固件时必须同时保留相应许可材料。

下面是本项目实机验证过的 Windows + WSL 快速流程，示例端口为 `COM5`：

1. 校验正式固件：

   ```bash
   cd dist/v0.2.0
   sha256sum --check SHA256SUMS
   cd ../..
   ```

2. Type-C 保持连接，长按 `PWR` 关机；按住 `BOOT`，短按 `PWR`，约 2 秒后松开
   `BOOT`。

3. 烧录完整固件：

   ```bash
   ./scripts/flash.sh --port COM5 \
     --firmware dist/v0.2.0/esp32-rlcd-firmware-v0.2.0.bin \
     --confirm
   ```

4. 看到 `Hash of data verified.` 后，长按 `PWR` 关机；不要按 `BOOT`，短按 `PWR`
   正常启动。

5. 未安装独立 RTC 电池时，每次彻底断电后重新校时：

   ```bash
   ./scripts/set-rtc.sh --port COM5
   ```

脚本首次运行只会从 Espressif 官方发布页下载并校验 esptool，之后复用仓库外缓存。纯
Windows、Linux/macOS 的烧录命令、端口识别、哈希校验和故障排查，详见
[发布固件安装指南](docs/user-install.md)。

> PCF85063 使用独立的 `RTC_BAT` 接口保持时间，不使用 18650 主电池。若要关机保持时间，
> 必须按微雪规格连接 PH1.0 可充电 RTC 电池；不得使用不可充电的 CR1220。

## 开发

| 项目 | 当前配置 |
| --- | --- |
| 开发板 | Waveshare ESP32-S3-RLCD-4.2 |
| 显示屏 | 4.2 英寸、400 × 300、ST7305 全反射屏 |
| 板载器件 | PCF85063 RTC、SHTC3 温湿度传感器、GPIO4 电池 ADC |
| 存储 | 16 MB Flash、8 MB Octal PSRAM |
| SDK | ESP-IDF v5.5.3 |
| 已验证工作流 | WSL 编译，Windows COM 口烧录和串口校时 |

首次准备固定版本依赖：

```bash
./scripts/bootstrap.sh
```

已有环境可检查依赖，然后运行测试和完整构建：

```bash
./scripts/bootstrap.sh --check
./scripts/test.sh
./scripts/build.sh
```

主要构建产物位于 `build/`：

- `rlcd_firmware.bin`：应用镜像，不能烧录到 `0x0`；
- `rlcd_firmware_merged.bin`：Bootloader、分区表和应用组成的完整镜像；
- `SHA256SUMS`：本次构建产物的校验值。

构建不会连接或改写开发板。依赖管理、升级与正式发布流程详见
[开发与发布指南](docs/development.md)；本地构建的烧录流程详见
[开发烧录指南](docs/flashing.md)。

## 项目结构

```text
.
├── src/                     # 本项目维护的固件源码
│   ├── app/                 # 应用入口与 USB 串口校时
│   ├── board/               # 板级总线和引脚定义
│   ├── calendar/            # 离线公历转农历
│   ├── display/             # ST7305 显示界面
│   ├── power/               # 电池 ADC 与电量估算
│   ├── rtc/                 # PCF85063 RTC 驱动
│   └── sensors/             # SHTC3 传感器驱动
├── dist/                    # 由本项目构建并实机验证的完整固件
├── scripts/                 # 依赖准备、测试、构建、烧录和校时脚本
├── tests/                   # 可在 WSL 直接运行的纯逻辑测试
├── docs/                    # 使用、开发、布局与实机验证文档
├── LICENSES/                # 随固件分发的第三方完整许可文本
├── CONTRIBUTING.md          # 贡献流程与仓库内容要求
├── CHANGELOG.md
├── LICENSE                  # 本项目 Apache License 2.0
├── NOTICE.md                # 固件组件、字体、版权与许可索引
├── SECURITY.md              # 安全问题报告方式
├── CMakeLists.txt
├── sdkconfig.defaults
└── tool-versions.env
```

构建目录、机器配置、第三方源码和社区固件不纳入版本控制。第三方组件的来源及许可索引
见[第三方软件与字体声明](NOTICE.md)，必须随二进制分发的完整文本保存在
[`LICENSES/`](LICENSES/)。许可文本是合规文档，不是第三方源码。

## 文档

- [发布固件安装指南](docs/user-install.md)
- [开发烧录流程与故障排查](docs/flashing.md)
- [开发、依赖与发布流程](docs/development.md)
- [首屏信息与布局规范](docs/home-screen.md)
- [实机验证记录](docs/bringup-log.md)
- [版本变更记录](CHANGELOG.md)
- [v0.2.0 固件说明与校验值](dist/v0.2.0/README.md)
- [参与贡献](CONTRIBUTING.md)
- [安全策略](SECURITY.md)
- [第三方软件与字体声明](NOTICE.md)

## 许可证

本项目自行编写的源码、脚本和文档由 taifu 于 2026 年以
[Apache License 2.0](LICENSE) 开放。第三方组件和字体继续适用各自许可证；发布固件的
完整归属和许可文本见 [`NOTICE.md`](NOTICE.md) 与 [`LICENSES/`](LICENSES/)。

## 参考资料

以下为本项目实际参考过的技术与硬件资料：

- [微雪 ESP32-S3-RLCD-4.2 产品文档](https://docs.waveshare.net/ESP32-S3-RLCD-4.2/)：
  硬件组成、按键、电源与 RTC 电池要求；
- [微雪 ESP32-S3-RLCD-4.2 官方仓库](https://github.com/waveshareteam/ESP32-S3-RLCD-4.2)：
  板级示例、引脚定义及固定版本的 ST7305/U8g2 外部构建组件；
- [微雪 RLCD 外设教程](https://docs.waveshare.net/ESP32-Peripheral-Tutorials/Display/RLCD/)：
  ST7305、U8g2、横屏方向与 SPI 接线；
- [微雪 RLCD Voice 外设示例](https://docs.waveshare.net/ESP32-ESPHome-Tutorials/Example-RLCD-Voice/)：
  用于确认 GPIO4 电池采样和三倍分压；
- [ESP-IDF v5.5.3 ESP32-S3 编程指南](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32s3/)；
- [Espressif esptool 文档](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/)：
  合并镜像、`write-flash` 与下载模式；
- [U8g2 上游项目](https://github.com/olikraus/u8g2)；
- [NXP PCF85063A 数据手册](https://www.nxp.com/docs/en/data-sheet/PCF85063A.pdf)；
- [Sensirion SHTC3 产品资料](https://sensirion.com/products/catalog/SHTC3)；
- [香港天文台公历与农历对照表](https://www.hko.gov.hk/sc/gts/time/conversion.htm)：
  用于核对农历日期测试数据。
