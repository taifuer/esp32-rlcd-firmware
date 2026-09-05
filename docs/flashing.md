# 开发烧录流程（WSL + Windows COM）

本文是本机开发构建的固定烧录流程。普通用户直接安装 `dist/` 中的发布固件时，优先阅读
[发布固件安装指南](user-install.md)。不要再把 USBIP 直通作为本机日常路径。

## 为什么使用 Windows COM

开发板的原生 USB Serial/JTAG 在 Windows 中稳定显示为 `COM5`，设备标识为：

```text
VID:PID 303a:1001
USB Serial/JTAG
```

这台机器此前尝试把设备通过 `usbipd` 交给 WSL 时遇到
`VDI_USB_HUB_FILTER`，而 Windows 直接访问 COM5 已连续完成外部测试固件和本项目固件
烧录。因此固定为：WSL 编译，Windows esptool 访问 COM。

COM 号可能变化，但 VID/PID 不应变化。`flash.sh` 会在写入前强制核对设备身份。

## 工具只下载一次

`./scripts/flash.sh` 会调用 `install-esptool-windows.sh`。首次运行时，它从 Espressif
官方 GitHub Release 下载 Windows esptool v5.3.1，并核对下载压缩包的 SHA-256：

```text
2b4a73c45db27426685896f64ce3e557f63a64f43cc100cb65c0cc3486af96d3
```

只保留 `esptool.exe`、上游 README、LICENSE 和本地校验文件，下载 ZIP 随即删除。
缓存位于仓库外：

```text
$RLCD_DEPS_DIR/toolchains/esptool-windows-v5.3.1/
```

以后每次烧录只校验并复用缓存，不重新下载。缓存损坏时脚本会停止，不会带病烧录。

## 先选择恢复路径

应用仍能启动时，优先使用设备内恢复，不必立即进入 ROM 下载模式：长按 `PWR` 关机，
按住 `KEY` 再短按 `PWR` 开机，看到 `RECOVERY MODE` 后松开 `KEY`。恢复模式只保留在线
更新和设置；设置页按住 `KEY` 3 秒可打开仅含 Wi-Fi 与本地 OTA 的精简门户。

`KEY` + `PWR` 是应用恢复，仍会运行屏幕和更新服务。`BOOT` + `PWR` 才是芯片 ROM 下载
模式，用于首次安装、分区迁移、应用恢复也无法进入或必须写入 Factory 的情况。恢复模式
自动触发与 OTA 健康门槛见[固件安装与更新](firmware-update.md#应用启动恢复)。

## 标准步骤

### 1. 构建

```bash
./scripts/build.sh
```

确认输出包含：

```text
Wrote ... rlcd_firmware_factory.bin, ready to flash to offset 0x0
```

构建目录同时生成：

```text
rlcd_firmware_factory.bin  # 首次安装、分区迁移与恢复
rlcd_firmware_ota.bin      # 在线更新、设置门户本地 OTA 或保留配置的串行应用更新
srmodels/srmodels.bin      # v0.18.0+ 离线语音模型，串行 app+model 更新使用
```

### 2. 进入 ROM 下载模式

1. Type-C 保持连接。
2. 长按 `PWR` 关机。
3. 按住 `BOOT`。
4. 短按 `PWR` 开机。
5. 等约 2 秒，松开 `BOOT`。

这只会让开发板进入 ESP32-S3 的 ROM 下载模式，不需要重启 Windows 或 WSL。
不要把这里的 `BOOT` 换成 `KEY`；后者会选择应用恢复模式，esptool 无法在该模式下写入
`0x0`。

### 3. 首次安装、分区迁移或恢复

```bash
./scripts/flash.sh --port COM5 --confirm
```

`--confirm` 是防止误烧的强制开关。脚本使用的关键参数固定为：

```text
chip: esp32s3
address: 0x0
flash mode: dio
flash frequency: 80 MHz
flash size: 16 MB
baud: 460800
before: no-reset
after: no-reset
```

只允许使用 `rlcd_firmware_factory.bin` 写入 `0x0`。单独的
`rlcd_firmware_ota.bin` 是 OTA 应用镜像，不能拿它写 `0x0`。`flash.sh` 会拒绝这种混用。

写入完成的成功标志：

```text
Hash of data verified.
```

脚本不会执行全片擦除，只擦除合并镜像覆盖的启动、分区和应用区域。

Factory 镜像是带 `0xFF` 填充的 raw 文件，覆盖范围包含位于 `0x9000` 的 NVS
分区。因此含联网功能的版本在按本流程烧录后会清除已保存的 Wi-Fi 凭据，首次启动需要
重新配网。它不会清除映像覆盖范围以外的整片 Flash，也不等同于 `erase-flash`。

### 4. 保留 Wi-Fi 的串行应用与模型更新

仅当设备已经完整安装 v0.7.0 或更新版本的双 OTA 应用布局，而且本次不需要更新
Bootloader 时使用。v0.18.0 新增的模型区域位于旧布局原本未分配的固定地址，不要求先
改写旧分区表：

```bash
./scripts/update-app.sh --port COM5 --confirm
```

脚本先验证 `build/SHA256SUMS`，然后在同一次 `write-flash` 中把
`rlcd_firmware_ota.bin` 写入固定的 `ota_0` 地址 `0x10000`，把
`build/srmodels/srmodels.bin` 写入固定模型地址 `0x610000`；两个文件都不得超过各自的
`0x300000` 区域。写入成功后只清除 `0xd000` 起的 8 KiB OTA 选择数据，使 Bootloader
下次选择 `ota_0`。它不会触碰 `0x9000` 的 NVS，所以家庭 Wi-Fi 和设备偏好保持不变。
使用发布目录时可分别通过 `--firmware` 与 `--model` 指定已列入同一 `SHA256SUMS` 的文件。
此流程仍需手动进入 ROM 下载模式。

仓库中的 v0.26.0 正式发布目录可以直接使用：

```bash
./scripts/update-app.sh \
  --port COM5 \
  --firmware dist/v0.26.0/esp32-rlcd-firmware-v0.26.0-ota.bin \
  --model dist/v0.26.0/esp32-rlcd-firmware-v0.26.0-model.bin \
  --confirm
```

发布目录中的 `SHA256SUMS` 同时覆盖 Factory、OTA 和模型；如果同版本的 `-model.bin` 与
`-ota.bin` 位于同一目录，脚本也能自动找到模型，可省略 `--model`。

旧设备迁移到 v0.26.0 且首次启用自 v0.18.0 起提供的离线语音时，需要执行一次本流程来
安装模型；在线更新和设置门户本地 OTA 仍只更新应用。模型已经安装后，普通用户和日常
开发继续优先使用设备内更新，无需数据线和 ROM 下载模式。完整步骤见
[固件安装与更新](firmware-update.md)。

### 构建版本与在线通道

仓库默认生成 `0.26.0`。构建后续预发布候选时使用环境变量，例如：

```bash
RLCD_PROJECT_VERSION=0.27.0-dev.1 ./scripts/build.sh
```

所有版本默认读取 `stable.json`，只有在设备设置中显式开启 Beta 更新后才读取
`testing.json`；版本名本身不切换通道。稳定清单只允许正式目标，测试清单用于预发布候选
和转正式指针。不要为了测试临时修改正式清单，也不要把前导 `v` 写进
`RLCD_PROJECT_VERSION`。

v0.9.0 没有在线更新客户端，可从原系统中心最后一页的本地更新入口上传当前 OTA，或按
本页的串行应用更新流程写入。设备显式加入测试通道后，可通过 `testing.json` 验证后续
预发布版本的在线更新。

### 5. 正常启动

烧录脚本有意让芯片留在下载模式，避免 USB 控制线导致“看起来复位了，实际仍在
Bootloader”的歧义。

1. 长按 `PWR` 关机。
2. 不要触碰 `BOOT`。
3. 短按 `PWR` 正常开机。

### 6. 校时与验收

烧录含自动网络校时的版本时，先按屏幕完成配网并确认 SNTP 能自动写入 RTC，不要先用
USB 手动校时掩盖联网问题。烧录旧的离线版本，或需要后备恢复时，执行：

```bash
./scripts/set-rtc.sh --port COM5
```

验收条件：

- 启动日志中的版本与本次 `RLCD_PROJECT_VERSION` 一致；未覆盖时应为仓库默认版本；
- `NORMAL` 模式屏幕等大显示完整的 `HH:MM:SS`，`SAVING` 模式显示 `HH:MM`；
- 屏幕显示公历日期、中文星期、农历、温湿度和电池图标；
- 串口出现 `RTC_SET_OK`；
- 后续 RTC 秒数继续增加；
- PCF85063 与 SHTC3 读取正常；
- “状态”页首次显示 `RTC BACKUP: UNTESTED`，一次真实断电后按实际结果变为
  `VERIFIED` 或 `FAILED`；
- 串口中的电池电压处于合理范围，屏幕电量百分比能够显示。

含自动网络校时的版本还应按[自动配网与网络校时](network-time.md)完成首次配网、SNTP、
RTC 写后回读和错误密码恢复；验证 `BOOT` 的日常页面路径，以及 `KEY` 的“状态、对话、
设置、在线更新”四个系统页面。“状态”页长按 2 秒应手动校时；“对话”页长按 2 秒并
松开后应完成一次本地识别，`KEY` 可提前结束输入，`BOOT` 可取消；“设置”页长按 3 秒应
进入当前版本的[设置入口](device-settings.md)，并能从网页保存时区、温度单位、音量与更新通道，完成手机校时、清除 Wi-Fi 配置
和本地 OTA。“设置”页长按 `BOOT` 2 秒应切换手动提前省电。USB `RESET_WIFI` 也应只
清除项目网络配置。天气已配置时，还应在天气页按住 `KEY` 2 秒，分别验证 `NORMAL` 与
`SAVING` 的手动获取、固定布局和联网收尾。

语音可靠性候选可在 WSL 中用固定采集脚本记录本次启动的标量诊断信息：

```bash
./scripts/collect-voice-diagnostics.sh --port COM5
```

脚本会打开 Windows 原生串口，确认模型、引擎和麦克风就绪，指导一次预热后清空易失计数，
再逐项等待并关联固定 50 次会话。运行期间按[开发说明](development.md#语音可靠性候选验收)
完成设备操作。日志默认写入 `build/`，只保存主机测试标签、五行 `GET_VOICE` 标量快照、
复位与严重故障标记，不保存完整串口流、PCM、家庭 Wi-Fi 凭据或设置门户请求正文。

在线更新还需用两个递增的预发布版本验证完整路径：第一次通过设置门户本地 OTA 或串行
方式安装，
第二次由 `testing.json` 提供。确认按住 `KEY` 2 秒检查、发现版本后再次按住 2 秒进入
`REVIEW`、确认页按住 3 秒安装；自动联网只能检查，不能静默安装。至少覆盖断网、同版本、
降级、错误项目或硬件、错误 URL 主机、大小/SHA-256 不匹配、下载中断、写入进度、重启
确认和回滚。任何失败都不得改变当前可启动槽，设置门户本地 OTA 在无互联网时仍可用。

涉及启动恢复的候选还必须单独验证：同一镜像连续 3 次异常复位后，下一次启动进入恢复；
计划内 OTA 重启、断电、棕断、USB/JTAG 复位和新镜像不会误触发；`KEY` + `PWR` 可手动
进入，`BOOT` + `PWR` 仍进入 ROM；恢复模式只出现“在线更新”和“设置”，精简门户只能
管理 Wi-Fi 和本地 OTA。按键初始化不可用时应自动开放精简门户，避免恢复入口失去操作
方式。待验证 OTA 只有在显示和按键就绪、首轮周期任务完成且运行至少 15 秒后才确认，
确认失败每 5 秒重试，稳定运行满 60 秒才清除故障计数。完成目标板验证前，不得在
CHANGELOG 或实机记录中写成已通过。

不要把家庭 SSID、密码或配网页面请求正文复制到验收记录、截图或提交中。在线更新未在
目标开发板完成上述验收前，不得在 CHANGELOG 的 `Verified` 下声称已经通过实机验证。

未连接独立的可充电 RTC 备用电池时，长按 PWR 关机后 RTC 丢失时间属于预期硬件行为；
下一次开机会先记录 `RTC BACKUP: FAILED`，随后才允许网络重新校时。

## 常见问题

### COM5 不存在

在 Windows PowerShell 执行：

```powershell
Get-CimInstance Win32_SerialPort | Select-Object DeviceID,Name,PNPDeviceID
```

找到包含 `VID_303A&PID_1001` 的端口，并把实际端口传给 `--port`。

### esptool 无法连接

大概率没有正确进入下载模式。重新执行“长按 PWR 关机 → 按住 BOOT → 短按 PWR →
两秒后松开 BOOT”，然后重试。不要直接执行全片擦除。

### 烧录成功但屏幕还是旧内容

全反射屏断电后可能保留旧画面，且芯片可能仍在 Bootloader。按标准步骤关机后正常开机，
等待数秒让新固件刷新屏幕。

### 软件复位后仍在下载模式

本板的 USB 控制线状态曾让 `hard-reset` 后仍留在 Bootloader。物理关机再正常开机是固定、
可观察的处理方式，不再临时尝试多套复位命令。

### 新应用反复复位

先用 `KEY` + `PWR` 进入应用恢复模式，尝试在线更新或精简门户本地 OTA。若应用恢复也无法
显示，再用 `BOOT` + `PWR` 进入 ROM 下载模式并执行 Factory 烧录。
