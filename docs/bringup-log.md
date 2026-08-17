# 实机 Bring-up 与操作记录

本文件保存可复用的硬件事实和已经发生的关键操作。日常流程仍以 `flashing.md` 为准。

## 硬件基线

| 项目 | 实测值 |
| --- | --- |
| 开发板 | Waveshare ESP32-S3-RLCD-4.2 |
| 芯片 | ESP32-S3 QFN56 rev 0.2 |
| Flash | 16 MB |
| PSRAM | 8 MB Octal PSRAM（AP，80 MHz） |
| Windows 端口 | COM5（可能随系统变化） |
| USB 标识 | `303a:1001`，USB Serial/JTAG |
| RTC | PCF85063，I2C `0x51` |
| 温湿度 | SHTC3，I2C `0x70`，ID `0x0887` |

## 2026-08-17：外部固件验证硬件

- 曾烧录 Weather Clock v1.5.37，以确认屏幕、音频和配网硬件可以工作。
- 该外部固件只保存在 Git 仓库外作为回退资料，不属于本项目源码或发布物。
- 随后转入完全自研的 ESP-IDF 固件路线。

## 2026-08-17：自研 v0.1.0

- 完成 ST7305 屏幕、8 MB PSRAM、PCF85063 和 SHTC3 的最小离线诊断。
- 实机启动成功，RTC 因首次使用/掉电标志显示 `RTC TIME IS NOT VALID`。
- 温湿度读取正常，SHTC3 ID 为 `0x0887`。
- Windows COM5 直接烧录可靠；WSL USBIP 因 `VDI_USB_HUB_FILTER` 不作为默认方案。

## 2026-08-17：自研 v0.1.1

### 烧录

- 目标确认：VID/PID `303a:1001`，ESP32-S3 rev 0.2，8 MB PSRAM。
- 完整合并镜像写入地址：`0x0`。
- 写入范围：`0x00000000` 至 `0x0005bfff`。
- 写入字节数：373,072。
- esptool 报告：`Hash of data verified.`
- 合并镜像 SHA-256：
  `5f8a4751fd0a72886ba7f9b344cfdf642afc7246f1401eeff48a6cd8cde8768d`。

### 启动与校时

- 启动日志确认 `App version: 0.1.1` 和 ESP-IDF v5.5.3。
- 8 MB PSRAM 内存测试通过。
- PCF85063 与 SHTC3 均被检测到。
- 发送：`SET_TIME 2026-08-17 22:33:20`。
- 回执：`RTC_SET_OK 2026-08-17 22:33:20 weekday=1`。
- 三秒后读取：`RTC 2026-08-17 22:33:23`，证明 RTC 正常递增。
- 屏幕实机确认可见 `Hello, world.`。

### 复位观察

- 普通 USB `hard-reset` 曾让芯片继续停在 Bootloader。
- Watchdog reset 可以启动应用，但 Windows 端口重枚举可能让 esptool 返回串口异常。
- 因此标准流程固定为：烧录后物理关机，再在不按 BOOT 的情况下正常开机。
