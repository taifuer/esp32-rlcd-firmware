# 发布固件安装指南

本文面向只想使用发布固件、不参与源码开发的用户。开发者请参阅
[开发与发布指南](development.md)和[开发烧录指南](flashing.md)。

## 选择正确文件

当前正式发布物可从
[GitHub Releases](https://github.com/taifuer/esp32-rlcd-firmware/releases/latest) 下载，
仓库内对应文件是
[`dist/v0.4.0/esp32-rlcd-firmware-v0.4.0.bin`](../dist/v0.4.0/esp32-rlcd-firmware-v0.4.0.bin)。
GitHub Releases 只保留最新正式版本，历史版本继续在 `dist/` 中按版本保存。
每个正式版本目录都应同时包含完整 `.bin`、`SHA256SUMS` 和版本说明。
从 v0.2.0 开始，版本目录还包含对应的首屏效果图。
下文命令假定终端当前位于仓库根目录，安装其他版本时同步替换目录和文件名。

GitHub Release 还包含 `LICENSE`、`NOTICE.md`、完整许可文本压缩包及
`RELEASE_SHA256SUMS`。烧录设备时主要使用 `.bin` 与 `SHA256SUMS`；如果复制或再分发
固件，必须同时保留 Release 中的许可材料。

| 项目 | 值 |
| --- | --- |
| 目标硬件 | Waveshare ESP32-S3-RLCD-4.2 |
| 固件类型 | Bootloader、分区表和应用组成的完整合并镜像 |
| 烧录地址 | `0x0` |
| Flash 参数 | DIO、80 MHz、16 MB |
| SHA-256 | 见 [`dist/v0.4.0/SHA256SUMS`](../dist/v0.4.0/SHA256SUMS) |

不要把仅包含应用的 `rlcd_firmware.bin` 写到 `0x0`。合并镜像也不是 BIOS：ESP32-S3
芯片内置的 ROM 下载程序不会被这个文件替换。

## 烧录前检查

1. 确认开发板型号完全一致。
2. 使用可传输数据的 Type-C 线连接电脑。
3. 在 Windows 设备管理器中找到 `USB JTAG/serial debug unit`，并记下 COM 号。
4. 正常连接时设备 VID/PID 应为 `303a:1001`。
5. 校验固件哈希；不匹配时停止烧录并重新下载。

WSL、Linux 可执行：

```bash
cd dist/v0.4.0
sha256sum --check SHA256SUMS
cd ../..
```

macOS 可执行：

```bash
shasum -a 256 dist/v0.4.0/esp32-rlcd-firmware-v0.4.0.bin
```

Windows PowerShell 可执行：

```powershell
(Get-FileHash .\dist\v0.4.0\esp32-rlcd-firmware-v0.4.0.bin -Algorithm SHA256).Hash.ToLower()
Get-Content .\dist\v0.4.0\SHA256SUMS
```

## 进入 ROM 下载模式

保持 Type-C 连接，然后按固定顺序操作：

1. 长按 `PWR` 关机；
2. 按住 `BOOT`；
3. 短按 `PWR` 开机；
4. 等待约 2 秒后松开 `BOOT`。

这只是在开发板上选择 ESP32-S3 ROM 下载模式，不需要进入电脑 BIOS，也不需要重启
Windows 或 WSL。

## 方法一：Windows + WSL 项目脚本（已实机验证）

在仓库根目录执行：

```bash
./scripts/flash.sh --port COM5 \
  --firmware dist/v0.4.0/esp32-rlcd-firmware-v0.4.0.bin \
  --confirm
```

把 `COM5` 替换成实际端口。脚本会依次：

- 校验固件目录中的 `SHA256SUMS`；
- 核对 Windows COM 口的 VID/PID；
- 安装或复用经过校验的 Espressif esptool v5.3.1 仓库外缓存；
- 识别 ESP32-S3，并把完整镜像写入 `0x0`；
- 让 esptool 对已写数据执行哈希验证。

首次运行需要联网下载官方 esptool；以后不会每次重复下载。成功标志是：

```text
Hash of data verified.
```

## 方法二：纯 Windows + 官方 esptool

这条路线不需要 WSL。先安装 Python 3，然后在 PowerShell 安装本项目固定版本：

```powershell
py -m pip install "esptool==5.3.1"
py -m esptool --help
```

确认端口属于本开发板：

```powershell
Get-CimInstance Win32_SerialPort |
  Where-Object PNPDeviceID -Match 'VID_303A&PID_1001' |
  Select-Object DeviceID, Name, PNPDeviceID
```

进入下载模式后烧录：

```powershell
py -m esptool --chip esp32s3 --port COM5 --baud 460800 `
  --before no-reset --after no-reset write-flash `
  --flash-mode dio --flash-freq 80m --flash-size 16MB `
  0x0 .\dist\v0.4.0\esp32-rlcd-firmware-v0.4.0.bin
```

如果使用 Espressif 官方独立版 `esptool.exe`，只需把命令开头的 `py -m esptool`
替换为该程序路径。不要额外执行 `erase-flash`。

## Linux 与 macOS

Espressif 官方 esptool 命令同样可用，把 PowerShell 中的续行符改为 `\`，并将 `COM5`
替换成实际串口，例如 Linux 的 `/dev/ttyACM0` 或 macOS 的 `/dev/cu.usbmodem...`。
本项目目前只完成了 Windows COM 实机验证，因此其他系统应先用
`python3 -m esptool --port PORT chip-id` 核对目标，再执行相同的 `write-flash` 参数。

```bash
python3 -m pip install "esptool==5.3.1"
python3 -m esptool --chip esp32s3 --port /dev/ttyACM0 --baud 460800 \
  --before no-reset --after no-reset write-flash \
  --flash-mode dio --flash-freq 80m --flash-size 16MB \
  0x0 dist/v0.4.0/esp32-rlcd-firmware-v0.4.0.bin
```

## 烧录后启动

烧录命令有意使用 `--after no-reset`，芯片完成后仍处于下载模式：

1. 长按 `PWR` 关机；
2. 不要触碰 `BOOT`；
3. 短按 `PWR` 正常开机；
4. 等待数秒，让全反射屏完成刷新。

全反射屏断电后可能保留旧画面，所以“仍看到旧内容”不代表新固件没有写入。

## 首次配网与自动校时

标准完整镜像会清除原有 NVS。首次启动时，屏幕显示 `WI-FI SETUP`、二维码、临时热点
名称、8 位随机密码和 `http://192.168.4.1`：

1. 用支持 Wi-Fi 二维码的手机相机扫码并确认加入临时热点；小米 Android 手机已实机
   验证。无法扫码时，按屏幕显示的名称和密码手动连接。
2. 手机通常会自动打开配网页面；没有打开时，在浏览器访问 `http://192.168.4.1`。
3. 填写设备要连接的 2.4 GHz 家庭 Wi-Fi，选择“保存并校时”。
4. 临时热点关闭后，设备连接家庭 Wi-Fi、获取网络时间、写入 PCF85063，并回到仪表盘。

家庭 Wi-Fi 凭据保存在设备 NVS，日常只需配置一次。未连接独立可充电 RTC 电池时，彻底
断电会使 RTC 时间失效，但下次开机会用已保存的网络配置自动恢复时间。重新烧录完整镜像
或执行 USB `RESET_WIFI` 后才需要重新配网。

USB 手动校时保留为无法联网时的后备方式，可从 WSL 执行：

```bash
./scripts/set-rtc.sh --port COM5
```

纯 Windows PowerShell 可在仓库根目录执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\set-rtc-windows.ps1 -Port COM5 -ReadSeconds 8
```

脚本发送当前中国标准时间。出现 `RTC_SET_OK`，随后读取到的秒数继续递增，即表示校时
成功。直接运行 Windows 脚本前请自行确认 COM 口属于本开发板；WSL 包装脚本会自动核对
VID/PID。自动配网的完整行为和安全边界见[自动配网与网络校时](network-time.md)。

## 日常按键

开发板上三个按键的职责不同：

| 按键 | 正常运行时的用途 |
| --- | --- |
| `KEY` | 短按查看设备状态；再次短按返回，或等待 15 秒自动返回 |
| `KEY` 长按 | 1 秒后显示倒计时；持续满 5 秒清除本项目 Wi-Fi 并重启配网 |
| `PWR` | 开机或长按关机 |
| `BOOT` | 仅在烧录步骤中选择 ESP32-S3 ROM 下载模式 |

设备状态页显示固件、RTC、温湿度传感器、电池、网络和本次启动最近校时。长按 `KEY`
倒计时结束前松开会取消，不会修改 Wi-Fi；确认清除后需要按屏幕重新配网。该操作只清除
本项目的网络凭据，不是全片擦除。

## 常见问题

### 找不到串口

- 换用确认支持数据传输的 Type-C 线和电脑 USB 口；
- 在设备管理器中重新查找 `USB JTAG/serial debug unit`；
- COM 号会变化，不要固定照抄 `COM5`。

### esptool 无法连接

通常是没有正确进入下载模式。重新执行“长按 PWR 关机 → 按住 BOOT → 短按 PWR →
两秒后松开 BOOT”，再重试；不要以全片擦除作为第一步。

### 烧录成功但没有新画面

先按“烧录后启动”物理关机，再在不按 BOOT 的情况下正常开机。软件复位在本板上曾让
芯片继续停留在 Bootloader，物理电源操作是目前固定流程。

### 开机显示时间无效

首次启动请按屏幕完成配网；已有网络配置时等待设备自动连接并校时。网络不可用时再使用
USB 校时脚本。若希望关机仍保持时间，应按微雪产品文档使用 PH1.0 接口的兼容可充电 RTC
电池，不要接入不可充电纽扣电池。

## 参考资料

- [微雪产品文档：BOOT/PWR、USB 与 RTC 电池](https://docs.waveshare.net/ESP32-S3-RLCD-4.2/)
- [Espressif esptool 安装文档](https://docs.espressif.com/projects/esptool/en/latest/esp32/installation.html)
- [Espressif esptool `write-flash` 与 `merge-bin`](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/esptool/basic-commands.html)
- [Espressif ESP32-S3 下载模式说明](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/advanced-topics/boot-mode-selection.html)
