# Changelog

本项目的显著变化记录在此。版本号采用语义化版本格式。

## [Unreleased]

### Planned

- Wi-Fi 配网与 NTP 校时。
- 上海时区处理和 NTP 时间写回 PCF85063。
- 断网时继续使用外置 RTC。

## [0.1.1] - 2026-08-17

### Added

- USB Serial/JTAG 命令：`SET_TIME`、`GET_TIME`、`HELP`。
- Windows 上海时间校时脚本，支持日期校验、星期计算和写后回读。
- 屏幕底部 `Hello, world.`。
- 可重复的依赖检查、构建、Windows COM 烧录和 RTC 校时流程。
- 已实机验证的 v0.1.1 完整合并固件。

### Changed

- RTC 无效时显示 `CONNECT USB TO SET RTC`。
- PCF85063 写入遵循停止计数、整块写入七个时间寄存器、恢复计数的顺序。

### Verified

- ESP32-S3 rev 0.2、16 MB Flash、8 MB PSRAM。
- PCF85063 地址 `0x51`，SHTC3 地址 `0x70`、芯片 ID `0x0887`。
- `RTC_SET_OK 2026-08-17 22:33:20 weekday=1`，随后时间正常递增。

## [0.1.0] - 2026-08-17

### Added

- 原生 ESP-IDF 5.5.3 工程。
- ST7305 全反射屏诊断时钟界面。
- PCF85063 RTC 读取与掉电失效检查。
- SHTC3 温湿度读取和 CRC 校验。
- 离线启动日志与硬件故障隔离。
