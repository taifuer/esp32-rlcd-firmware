# 发布固件安装指南

本文面向只想使用发布固件、不参与源码开发的用户。开发者请参阅
[开发与发布指南](development.md)和[开发烧录指南](flashing.md)。

本文的下载与烧录示例以 v0.26.0 正式版为准；功能交互同时跟随当前源码。
AI 对话仍是默认关闭的 Beta 功能；未开启、离线或处于 `SAVING` 时继续使用本地
MultiNet 指令识别。

## 选择正确文件

当前正式发布物可从
[GitHub Releases](https://github.com/taifuer/esp32-rlcd-firmware/releases/latest) 下载，
仓库内对应文件是
[`dist/v0.26.0/`](../dist/v0.26.0/)。
GitHub Releases 只保留最新正式版本，历史版本继续在 `dist/` 中按版本保存。
v0.7.0 起每个正式版本目录同时包含 Factory、OTA、`SHA256SUMS` 和版本说明；v0.18.0
起还包含离线语音模型。
从 v0.2.0 开始，版本目录还包含对应的界面效果图。
下文命令假定终端当前位于仓库根目录，安装其他版本时同步替换目录和文件名。

GitHub Release 还包含 `LICENSE`、`NOTICE.md`、完整许可文本压缩包及
`RELEASE_SHA256SUMS`。烧录设备时主要使用 `.bin` 与 `SHA256SUMS`；如果复制或再分发
固件，必须同时保留 Release 中的许可材料。

| 文件 | 用途 | 安装方式 |
| --- | --- | --- |
| `esp32-rlcd-firmware-v0.26.0-factory.bin` | 首次安装、v0.6.0 及更早版本迁移、故障恢复 | ROM 下载模式写入 `0x0`，包含语音模型并清除 NVS |
| `esp32-rlcd-firmware-v0.26.0-ota.bin` | 已安装 v0.7.0+ 后的日常更新 | 在线更新、设置门户本地 OTA 或串行应用更新，保留 NVS |
| `esp32-rlcd-firmware-v0.26.0-model.bin` | 为旧设备补装自 v0.18.0 起提供的离线语音模型 | 仅与同版本 OTA 通过项目 USB 脚本写入，不可单独启动或上传网页 |

目标硬件为 Waveshare ESP32-S3-RLCD-4.2，Factory 镜像使用 DIO、80 MHz、16 MB Flash
参数；全部发布二进制的 SHA-256 见
[`dist/v0.26.0/SHA256SUMS`](../dist/v0.26.0/SHA256SUMS)。

不要把仅包含应用的 `-ota.bin` 写到 `0x0`，也不要在网页中上传 `-factory.bin`。Factory
镜像不是 BIOS：ESP32-S3 芯片内置的 ROM 下载程序不会被它替换。两类更新的完整说明见
[固件安装与更新](firmware-update.md)。

### 从旧版本升级

v0.7.0—v0.9.0 没有在线更新客户端，可从原系统中心最后一页的本地更新入口上传
v0.26.0 `-ota.bin`，或使用 USB 写入。v0.10.0 及更新版本可直接从 `ONLINE UPDATE`
升级到当前稳定版，不需要逐版本安装；v0.6.0 及更早版本必须使用 Factory 固件完整安装。

在线更新和网页本地 OTA 只更新应用，不写语音模型。从 v0.16.0 等旧正式版升级后，时钟、
低功耗及其他功能可以正常使用，但对话页会提示模型不可用。要使用离线语音指令，可执行
一次“USB 应用与模型更新”；该方法保留 Wi-Fi 和其他 NVS 设置。首次安装或允许重新配网时，
直接写入 v0.26.0 Factory 即可同时安装应用和模型。

设备默认只检查 `stable.json`；只有在设置门户明确开启 Beta 固件更新后才检查
`testing.json`，版本名本身不会切换通道。联网后的自动流程只记录检查结果，不会自动下载
或静默安装；完整操作见
[固件安装与更新](firmware-update.md)。

## 应用还能启动时先用恢复模式

安装支持启动恢复的固件后，同一应用镜像在健康门槛前连续异常复位 3 次，下一次启动会
自动进入 `RECOVERY MODE`。也可长按 `PWR` 关机，按住 `KEY` 再短按 `PWR` 开机，看到
恢复页面后松开 `KEY`。

恢复模式只保留“在线更新”和“设置”，短按 `KEY` 在两页间切换：

- 在“在线更新”按住 `KEY` 2 秒检查或查看版本，安装仍需在确认页按住 3 秒；
- 在“设置”按住 `KEY` 3 秒开启精简恢复门户，只管理 Wi-Fi 和本地 OTA；
- 修复完成后关机，不按 `KEY` 正常开机。

自动恢复时若实体按键初始化不可用，设备会直接开放精简门户；按屏幕显示的热点信息连接
即可。

精简门户上传的仍是 `-ota.bin`，不能上传 `-factory.bin`。如果应用恢复页也无法进入，再按
后文步骤使用 `BOOT` + `PWR` 进入 ROM 下载模式并写入 Factory 固件。恢复模式和 ROM 下载
模式是两条不同路径。

## 烧录前检查

1. 确认开发板型号完全一致。
2. 使用可传输数据的 Type-C 线连接电脑。
3. 在 Windows 设备管理器中找到 `USB JTAG/serial debug unit`，并记下 COM 号。
4. 正常连接时设备 VID/PID 应为 `303a:1001`。
5. 校验固件哈希；不匹配时停止烧录并重新下载。

WSL、Linux 可执行：

```bash
cd dist/v0.26.0
sha256sum --check SHA256SUMS
cd ../..
```

macOS 可执行：

```bash
shasum -a 256 dist/v0.26.0/esp32-rlcd-firmware-v0.26.0-factory.bin
shasum -a 256 dist/v0.26.0/esp32-rlcd-firmware-v0.26.0-ota.bin
shasum -a 256 dist/v0.26.0/esp32-rlcd-firmware-v0.26.0-model.bin
```

Windows PowerShell 可执行：

```powershell
(Get-FileHash .\dist\v0.26.0\esp32-rlcd-firmware-v0.26.0-factory.bin -Algorithm SHA256).Hash.ToLower()
(Get-FileHash .\dist\v0.26.0\esp32-rlcd-firmware-v0.26.0-ota.bin -Algorithm SHA256).Hash.ToLower()
(Get-FileHash .\dist\v0.26.0\esp32-rlcd-firmware-v0.26.0-model.bin -Algorithm SHA256).Hash.ToLower()
Get-Content .\dist\v0.26.0\SHA256SUMS
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
  --firmware dist/v0.26.0/esp32-rlcd-firmware-v0.26.0-factory.bin \
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
  0x0 .\dist\v0.26.0\esp32-rlcd-firmware-v0.26.0-factory.bin
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
  0x0 dist/v0.26.0/esp32-rlcd-firmware-v0.26.0-factory.bin
```

## 烧录后启动

烧录命令有意使用 `--after no-reset`，芯片完成后仍处于下载模式：

1. 长按 `PWR` 关机；
2. 不要触碰 `BOOT`；
3. 短按 `PWR` 正常开机；
4. 等待数秒，让全反射屏完成刷新。

全反射屏断电后可能保留旧画面，所以“仍看到旧内容”不代表新固件没有写入。

## 旧设备补装离线语音模型

已经运行 v0.7.0 或更新版本、并且希望保留 Wi-Fi 与设备偏好的设备，可通过 Windows + WSL
一次写入 v0.26.0 应用和同版本模型。离线语音模型自 v0.18.0 起提供。先把 OTA、模型和
`SHA256SUMS` 放在同一目录；从 Release 下载或使用仓库 `dist/v0.26.0/` 即已满足这一
条件。进入 ROM 下载模式后执行：

```bash
./scripts/update-app.sh --port COM5 \
  --firmware dist/v0.26.0/esp32-rlcd-firmware-v0.26.0-ota.bin \
  --confirm
```

脚本会自动找到同目录同版本的 `-model.bin`，校验两者后分别写入固定的应用槽和模型区域，
只重置 OTA 选择数据，不覆盖 NVS。烧录完成后按上一节的步骤物理关机并正常开机。该方法
只适用于已经具有 v0.7.0+ 分区布局的设备；更早版本仍须写入 Factory。

## 首次配网与自动校时

标准完整镜像会清除原有 NVS。首次启动时，屏幕显示 `WI-FI SETUP`、二维码、临时热点
名称、8 位随机密码和 `http://192.168.4.1`：

1. 用支持 Wi-Fi 二维码的手机相机扫码并确认加入临时热点；小米 Android 手机已实机
   验证。无法扫码时，按屏幕显示的名称和密码手动连接。
2. 手机通常会自动打开配网页面；没有打开时，在浏览器访问 `http://192.168.4.1`。
3. 填写设备要连接的 2.4 GHz 家庭 Wi-Fi，选择“保存并校时”。
4. 临时热点关闭后，设备连接家庭 Wi-Fi、获取网络时间、写入 PCF85063，并回到仪表盘。
   实际状态为 `NORMAL` 时会保持连接；`SAVING` 只完成本次校时后关闭无线。

家庭 Wi-Fi 凭据保存在设备 NVS，日常只需配置一次。未连接独立可充电 RTC 电池时，彻底
断电会使 RTC 时间失效，但下次开机会用已保存的网络配置自动恢复时间。重新烧录完整镜像
或执行 USB `RESET_WIFI` 后才需要重新配网。

配网热点最多开放 5 分钟，二维码显示 60 秒后设备会恢复首屏；没有网络时也可按屏幕上的
`BOOT: OFFLINE` 立即使用本地功能。热点超时后需要配网可重新开机。
已保存网络的设备在 Wi-Fi 或互联网不可用时不会卡在连接界面，也不会自动清除凭据或反复
进入配网：RTC、月历、温湿度、电量和离线语音继续正常使用。实际 `NORMAL` 会保持家庭
Wi-Fi 连接，断线后按 1、5、15 分钟退避，之后每 15 分钟重试；实际 `SAVING` 不自动
联网。
RTC 同时无效且无法联网时，首屏显示 `--:--:--`，可使用下方 USB 后备流程校时。

安装 RTC 备用电池后，“状态”页 `RTC` 行中的备用结果首先显示 `UNTESTED`。保持 RTC 已校时，
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
| `BOOT` | 日常页面中短按切换首屏、已开启的天气、月历，以及存在有效 microSD 图片时的图片页；系统中心中短按返回首屏；除“设置”页外运行时长按无操作 |
| `KEY` | 主界面、天气和日历中短按进入系统中心；多图图片页中短按切换下一张，单图时进入系统中心；系统中心中短按依次切换“状态 → 对话 → 设置 → 在线更新” |
| `KEY` 长按 | 天气页按住 2 秒刷新；图片页按住 2 秒进入删除确认；“状态”页按住 2 秒校时；“对话”页按住 2 秒并松开后开始会话；“设置”页按住 3 秒打开设置入口（版本区别见下文）；“在线更新”页按住 2 秒检查或进入 `REVIEW`，确认页按住 3 秒安装 |
| `BOOT` 长按 | 仅在“设置”页按住 2 秒切换手动省电覆盖；其他运行页面不执行隐藏操作 |
| `PWR` | 开机或长按关机 |

v0.26.0 起，切到哪一页就停在哪一页，由用户手动切换，不需要另设默认页；
正常开机仍显示时钟。入口与按键见
[设备设置](device-settings.md)。在线更新的检查、连接和确认阶段可按 `BOOT`
取消；开始写入后按键不再中断。“状态”页集中显示 RTC、RTC 备用电池、传感器、电池和
最近时间同步结果，并用独立的 `WI-FI` 行显示当前链路。

图片删除确认页必须先完整松开发起长按的 `KEY`，再短按 `KEY` 才会删除；短按 `BOOT`
或等待 10 秒取消。删除完成后无需重启，详细行为见[microSD 图片](microsd-images.md)。

“对话”页按住 `KEY` 2 秒进入，屏幕提示后松开再说话；监听时短按 `KEY` 提前结束输入，
连接、监听、识别和结果期间短按 `BOOT` 取消。AI 对话已开启、API Key 已保存、设备处于
`NORMAL` 且家庭 Wi-Fi 当前在线时，设备在同一 WebSocket 中最多保留 5 轮
上下文，每轮输入最长 10 秒；未配置、停用、离线或 `SAVING` 时直接使用本地模式，
不等待云端超时。回复完成后 30 秒内短按 `KEY` 续问，短按 `BOOT` 结束；新一轮可在
会话开始后的 5 分钟内发起，正在进行的收音和回答不会被总时限截断。轮次未满时，播放中
短按 `KEY` 会先跳过当前回复，短暂显示 `NEXT TURN` 后再进入下一轮。回答默认简短，
文字跟随本地播放进度滚动显示最新 4 行。会话仍为半双工，结束后不跨次保留上下文；设备
也没有唤醒词或后台麦克风。

本地模式支持“回到主页 / 查看时间”“打开日历 / 查看日期”“查看状态 / 设备状态”、
“打开图片 / 查看图片”“打开设置 / 查看设置”和“取消”。云端回复只展示和播放，不执行
这些指令或其他设备动作。完整配置、数据上传、凭据和费用说明见
[AI 对话 Beta](cloud-voice.md)。

按[网页设置入口](device-settings.md#网页设置)打开门户后，设备会开启最多 5 分钟的临时 WPA2 热点并显示二维码、随机
密码和 `http://192.168.4.1`。手机进入“设备设置”页面后可以：

- 修改时区、摄氏/华氏温标、回放音量、单个每周闹钟和 Beta 固件更新偏好；
- 可开启或停用 AI 对话、保存或清除 API Key，并在高级设置中从
  `qwen3-omni-flash-realtime`（默认）和 `qwen-audio-3.0-realtime-flash` 中选择；API
  Host 留空使用北京共享域名，也可填写北京或新加坡的百炼官方共享/业务空间专属 Host。
  只填写主机名，不含 `wss://`、路径或端口；设置页不接受独立 Workspace ID、App ID、
  完整 URL 或任意域名；
- 使用手机当前时间校准 RTC，或恢复偏好默认值；
- 查看已保存的家庭 Wi-Fi 名称，并在不预填密码的情况下验证、更换网络；
- v0.15.0 起可导入、逐张预览、选择和二次确认删除 microSD 图片；
- 清除家庭 Wi-Fi 配置；该操作不会删除其他偏好，设备不重启，设置热点关闭后原地进入配网；
- 上传本项目发布的 `-ota.bin` 完成本地升级；不要上传 `-factory.bin`。

电源模式无需进入热点设置：设备根据电量和 USB 数据主机状态自动决定，低电时进入
`SAVING`，连接电脑 USB 时立即退出；“设置”页按住 `BOOT` 2 秒可手动提前进入或取消
省电覆盖。`NORMAL` 使用 ESP-IDF `WIFI_PS_MIN_MODEM` 保持家庭 Wi-Fi 连接；`SAVING`
停止后台 Wi-Fi、隐藏秒数并降低刷新频率，需要联网的主动操作只在期间临时连接。设置
门户关闭后同样按最终实际状态保持或关闭家庭连接。电量百分比来自电压估算，插拔电源后的
短时跳变不等同于实际容量瞬间变化。

Beta 固件更新默认关闭，仅建议能够使用本地 OTA 或 USB 恢复设备的开发者开启。普通偏好和
恢复默认值保存后直接生效，设置热点保持连接；清除 Wi-Fi 会切换热点，手机需要按屏幕
提示重新连接。只有本地 OTA 校验成功后会自动切换到新槽并重启。设置门户和本地 OTA
不依赖家庭 Wi-Fi 或互联网。

闹钟按 RTC 本地时间和勾选的星期运行，断网及 `SAVING` 模式不影响已保存的规则。到点后
短按 `BOOT` 停止；首次响铃可短按 `KEY` 延后 5 分钟，单次响铃最长 60 秒。物理关机时
设备与扬声器没有供电，因此不会响铃，也不会在下次开机补发。

`BOOT` 在开机时和正常运行时具有不同含义：关机后按住它再按 `PWR` 会选择 ROM 下载
模式；固件正常运行后短按用于页面导航或取消，只有“设置”页定义了长按动作。
关机后按住 `KEY` 再按 `PWR` 则选择应用恢复模式，不会进入 ROM 下载程序。

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

### 新版本反复重启

支持启动恢复的版本会在同一镜像连续 3 次异常复位后的下一次启动进入 `RECOVERY MODE`；
也可关机后按住 `KEY` 再短按 `PWR` 主动进入。先尝试在线更新，或从精简恢复门户上传正确
的 `-ota.bin`。如果恢复页面也无法进入，再使用 ROM 下载模式写入 `-factory.bin`。

### 开机显示时间无效

首次启动请按屏幕完成配网；已有网络配置时等待设备自动连接并校时。网络不可用时再使用
USB 校时脚本。若希望关机仍保持时间，应按微雪产品文档使用 PH1.0 接口的兼容可充电 RTC
电池，不要接入不可充电纽扣电池。安装后按“首次配网与自动校时”的断电步骤检查“状态”
页；`UNTESTED` 表示尚未完成一次有效断电测试，不是故障。

### 对话页显示模型不可用

这通常表示设备从旧版本仅通过在线更新或网页 OTA 安装了 v0.26.0 应用。其他功能不受
影响；按“旧设备补装离线语音模型”通过 USB 同时写入同版本 OTA 和模型，或使用 Factory
重新完整安装。`-model.bin` 不能单独启动，也不能上传到设置门户。

## 参考资料

- [微雪产品文档：BOOT/PWR、USB 与 RTC 电池](https://docs.waveshare.net/ESP32-S3-RLCD-4.2/)
- [Espressif esptool 安装文档](https://docs.espressif.com/projects/esptool/en/latest/esp32/installation.html)
- [Espressif esptool `write-flash` 与 `merge-bin`](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/esptool/basic-commands.html)
- [Espressif ESP32-S3 下载模式说明](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/advanced-topics/boot-mode-selection.html)
