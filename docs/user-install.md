# 发布固件安装指南

本文面向只想使用发布固件、不参与源码开发的用户。开发者请参阅
[开发与发布指南](development.md)和[开发烧录指南](flashing.md)。

## 选择正确文件

当前正式发布物可从
[GitHub Releases](https://github.com/taifuer/esp32-rlcd-firmware/releases/latest) 下载，
仓库内对应文件是
[`dist/v0.15.0/`](../dist/v0.15.0/)。
GitHub Releases 只保留最新正式版本，历史版本继续在 `dist/` 中按版本保存。
v0.7.0 起每个正式版本目录同时包含 Factory、OTA、`SHA256SUMS` 和版本说明。
从 v0.2.0 开始，版本目录还包含对应的界面效果图。
下文命令假定终端当前位于仓库根目录，安装其他版本时同步替换目录和文件名。

GitHub Release 还包含 `LICENSE`、`NOTICE.md`、完整许可文本压缩包及
`RELEASE_SHA256SUMS`。烧录设备时主要使用 `.bin` 与 `SHA256SUMS`；如果复制或再分发
固件，必须同时保留 Release 中的许可材料。

| 文件 | 用途 | 安装方式 |
| --- | --- | --- |
| `esp32-rlcd-firmware-v0.15.0-factory.bin` | 首次安装、v0.6.0 及更早版本迁移、故障恢复 | ROM 下载模式写入 `0x0`，会清除 NVS |
| `esp32-rlcd-firmware-v0.15.0-ota.bin` | 已安装 v0.7.0+ 后的日常更新 | 在线更新、设置门户本地 OTA 或串行应用更新，保留 NVS |

目标硬件为 Waveshare ESP32-S3-RLCD-4.2，Factory 镜像使用 DIO、80 MHz、16 MB Flash
参数；两类文件的 SHA-256 见
[`dist/v0.15.0/SHA256SUMS`](../dist/v0.15.0/SHA256SUMS)。

不要把仅包含应用的 `-ota.bin` 写到 `0x0`，也不要在网页中上传 `-factory.bin`。Factory
镜像不是 BIOS：ESP32-S3 芯片内置的 ROM 下载程序不会被它替换。两类更新的完整说明见
[固件安装与更新](firmware-update.md)。

### 从旧版本升级

v0.7.0—v0.9.0 没有在线更新客户端，可从原系统中心最后一页的本地更新入口上传
v0.15.0 `-ota.bin`，或使用 USB 写入。v0.10.0—v0.14.0 可直接从 `ONLINE UPDATE`
升级；v0.6.0 及更早版本必须使用 Factory 固件完整安装。

设备默认只检查 `stable.json`；只有在设置门户明确开启 Beta 更新后才检查
`testing.json`，版本名本身不会切换通道。联网后的自动流程只记录检查结果，不会自动下载
或静默安装；完整操作见
[固件安装与更新](firmware-update.md)。

## 烧录前检查

1. 确认开发板型号完全一致。
2. 使用可传输数据的 Type-C 线连接电脑。
3. 在 Windows 设备管理器中找到 `USB JTAG/serial debug unit`，并记下 COM 号。
4. 正常连接时设备 VID/PID 应为 `303a:1001`。
5. 校验固件哈希；不匹配时停止烧录并重新下载。

WSL、Linux 可执行：

```bash
cd dist/v0.15.0
sha256sum --check SHA256SUMS
cd ../..
```

macOS 可执行：

```bash
shasum -a 256 dist/v0.15.0/esp32-rlcd-firmware-v0.15.0-factory.bin
shasum -a 256 dist/v0.15.0/esp32-rlcd-firmware-v0.15.0-ota.bin
```

Windows PowerShell 可执行：

```powershell
(Get-FileHash .\dist\v0.15.0\esp32-rlcd-firmware-v0.15.0-factory.bin -Algorithm SHA256).Hash.ToLower()
(Get-FileHash .\dist\v0.15.0\esp32-rlcd-firmware-v0.15.0-ota.bin -Algorithm SHA256).Hash.ToLower()
Get-Content .\dist\v0.15.0\SHA256SUMS
```

## 进入 ROM 下载模式

保持 Type-C 连接，然后按固定顺序操作：

1. 长按 `PWR` 关机；
2. 按住 `BOOT`；
3. 短按 `PWR` 开机；
4. 等待约 2 秒后松开 `BOOT`。

这只是在开发板上选择 ESP32-S3 ROM 下载模式，不需要重启 Windows 或 WSL。

## 方法一：Windows + WSL 项目脚本（已实机验证）

在仓库根目录执行：

```bash
./scripts/flash.sh --port COM5 \
  --firmware dist/v0.15.0/esp32-rlcd-firmware-v0.15.0-factory.bin \
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
  0x0 .\dist\v0.15.0\esp32-rlcd-firmware-v0.15.0-factory.bin
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
  0x0 dist/v0.15.0/esp32-rlcd-firmware-v0.15.0-factory.bin
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

配网热点最多开放 5 分钟，二维码显示 60 秒后设备会恢复首屏；没有网络时也可按屏幕上的
`BOOT: OFFLINE` 立即使用本地功能。热点超时后需要配网可重新开机。
已保存网络的设备在 Wi-Fi 或互联网不可用时不会卡在连接界面，也不会自动清除凭据或反复
进入配网：RTC、月历、温湿度、电量和音频继续离线运行，网络在后台按逐步延长的间隔重试。
RTC 同时无效且无法联网时，首屏显示 `--:--:--`，可使用下方 USB 后备流程校时。

安装 RTC 备用电池后，“状态”页的 `RTC BACKUP` 首先显示 `UNTESTED`。保持 RTC 已校时，
拔掉 Type-C 后长按 `PWR` 关机，等待至少一分钟，再在不按 `BOOT` 的情况下短按 `PWR`
开机；固件会在联网前自动判断并显示 `VERIFIED` 或 `FAILED`。该状态不等同于电池电量，
只表示最近一次断电是否保持了 RTC。

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
| `BOOT` | 日常页面中短按切换首屏和月历；microSD 有有效图片时条件加入图片页；系统中心中短按返回首屏；运行时长按无操作 |
| `KEY` | 主界面和日历中短按进入系统中心；v0.15.0 多图图片页中短按切换下一张，单图时进入系统中心；系统中心中短按依次切换“状态 → 音频 → 设置 → 在线更新” |
| `KEY` 长按 | “状态”页按住 2 秒立即校时；“音频”页按住 2 秒开始临时语音回放测试；“设置”页按住 3 秒开启设置门户；“在线更新”页按住 2 秒检查或进入 `REVIEW`，确认页按住 3 秒安装；其他页面无操作 |
| `PWR` | 开机或长按关机 |

月历、图片和系统页停留 30 秒会自动回到首屏。在线更新的检查、连接和确认阶段可按 `BOOT`
取消；开始写入后按键不再中断。“状态”页集中显示 RTC、RTC 备用电池、传感器、电池和
网络校时摘要。

“设置”页按住 `KEY` 3 秒后，设备会开启最多 5 分钟的临时 WPA2 热点并显示二维码、随机
密码和 `http://192.168.4.1`。手机进入“设备设置”页面后可以：

- 修改 `NORMAL / SAVING`、时区、摄氏/华氏温标、回放音量、单个每周闹钟和 Beta 更新偏好；
- 使用手机当前时间校准 RTC，或恢复偏好默认值；
- v0.15.0 起可导入、逐张预览、选择和二次确认删除 microSD 图片；
- 清除家庭 Wi-Fi 配置；该操作不会删除其他偏好，设备不重启，设置热点关闭后原地进入配网；
- 上传本项目发布的 `-ota.bin` 完成本地升级；不要上传 `-factory.bin`。

Beta 更新默认关闭，仅建议能够使用本地 OTA 或 USB 恢复设备的开发者开启。普通偏好和
恢复默认值保存后直接生效，设置热点保持连接；清除 Wi-Fi 会切换热点，手机需要按屏幕
提示重新连接。只有本地 OTA 校验成功后会自动切换到新槽并重启。设置门户和本地 OTA
不依赖家庭 Wi-Fi 或互联网。

闹钟按 RTC 本地时间和勾选的星期运行，断网及 `SAVING` 模式不影响已保存的规则。到点后
短按 `BOOT` 停止；首次响铃可短按 `KEY` 延后 5 分钟，单次响铃最长 60 秒。物理关机时
设备与扬声器没有供电，因此不会响铃，也不会在下次开机补发。

音频测试会先播放两声提示音，再录制最多 5 秒并自动回放；录制或回放时短按 `KEY` 可
提前结束当前阶段，短按 `BOOT` 可取消整个测试。语音只临时存放在 PSRAM，结束后立即
清除，不会写入设备存储或上传网络。播放音量为 `0%` 时不会启用扬声器，但仍会完成录音
和双麦克风分析；提示音不会播放，回放结果显示为 `NOT PLAYED`。

`BOOT` 在开机时和正常运行时具有不同含义：关机后按住它再按 `PWR` 会选择 ROM 下载
模式；固件正常运行后只处理短按页面导航，长按不执行操作。

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
电池，不要接入不可充电纽扣电池。安装后按“首次配网与自动校时”的断电步骤检查“状态”
页；`UNTESTED` 表示尚未完成一次有效断电测试，不是故障。

## 参考资料

- [微雪产品文档：BOOT/PWR、USB 与 RTC 电池](https://docs.waveshare.net/ESP32-S3-RLCD-4.2/)
- [Espressif esptool 安装文档](https://docs.espressif.com/projects/esptool/en/latest/esp32/installation.html)
- [Espressif esptool `write-flash` 与 `merge-bin`](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/esptool/basic-commands.html)
- [Espressif ESP32-S3 下载模式说明](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/advanced-topics/boot-mode-selection.html)
