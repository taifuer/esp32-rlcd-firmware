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

## 标准步骤

### 1. 构建

```bash
./scripts/build.sh
```

确认输出包含：

```text
Wrote ... rlcd_firmware_merged.bin, ready to flash to offset 0x0
```

### 2. 进入 ROM 下载模式

1. Type-C 保持连接。
2. 长按 `PWR` 关机。
3. 按住 `BOOT`。
4. 短按 `PWR` 开机。
5. 等约 2 秒，松开 `BOOT`。

不需要进入电脑 BIOS，也不需要重启 Windows 或 WSL。

### 3. 烧录

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

只允许使用 `rlcd_firmware_merged.bin` 写入 `0x0`。单独的
`rlcd_firmware.bin` 是应用镜像，其正确地址是 `0x10000`，不能拿它写 `0x0`。

写入完成的成功标志：

```text
Hash of data verified.
```

脚本不会执行全片擦除，只擦除合并镜像覆盖的启动、分区和应用区域。

完整合并镜像是带 `0xFF` 填充的 raw 文件，覆盖范围包含默认位于 `0x9000` 的 NVS
分区。因此含联网功能的版本在按本流程烧录后会清除已保存的 Wi-Fi 凭据，首次启动需要
重新配网。它不会清除映像覆盖范围以外的整片 Flash，也不等同于 `erase-flash`。

### 4. 正常启动

烧录脚本有意让芯片留在下载模式，避免 USB 控制线导致“看起来复位了，实际仍在
Bootloader”的歧义。

1. 长按 `PWR` 关机。
2. 不要触碰 `BOOT`。
3. 短按 `PWR` 正常开机。

### 5. 校时与验收

烧录含自动网络校时的版本时，先按屏幕完成配网并确认 SNTP 能自动写入 RTC，不要先用
USB 手动校时掩盖联网问题。烧录旧的离线版本，或需要后备恢复时，执行：

```bash
./scripts/set-rtc.sh --port COM5
```

验收条件：

- 启动日志中的版本与 `CMakeLists.txt` 的 `PROJECT_VER` 一致；
- 屏幕等大显示完整的 `HH:MM:SS`；
- 屏幕显示公历日期、中文星期、农历、温湿度和电池图标；
- 串口出现 `RTC_SET_OK`；
- 后续 RTC 秒数继续增加；
- PCF85063 与 SHTC3 读取正常；
- 串口中的电池电压处于合理范围，屏幕电量百分比能够显示。

含自动网络校时的版本还应按[自动配网与网络校时](network-time.md)完成首次配网、SNTP、
RTC 写后回读和错误密码恢复；验证 `BOOT` 的日常页面路径、`KEY` 的四个系统页面、网络页
2 秒手动校时，以及 Wi-Fi 维护页 5 秒前取消和满 5 秒重配网。USB `RESET_WIFI` 也应只
清除项目配置。不要把家庭 SSID、密码或配网页面请求正文复制到验收记录、截图或提交中。

未连接独立的可充电 RTC 备用电池时，长按 PWR 关机后 RTC 丢失时间属于预期硬件行为；
这不影响本次开机后的显示验收。

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
