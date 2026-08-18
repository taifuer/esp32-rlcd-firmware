# ESP32 RLCD Firmware v0.1.1

这是由本项目源码及固定开源依赖构建的 v0.1.1 实机验证发布物，不是对外部或社区完整
固件的改名或重新打包。

该版本保留早期诊断式首屏；当前新仪表盘已在 v0.2.0 正式发布。完整步骤见
[发布固件安装指南](../../docs/user-install.md)。

| 项目 | 值 |
| --- | --- |
| 文件 | `esp32-rlcd-firmware-v0.1.1.bin` |
| 烧录地址 | `0x0` |
| 大小 | 373,072 bytes |
| SHA-256 | `5f8a4751fd0a72886ba7f9b344cfdf642afc7246f1401eeff48a6cd8cde8768d` |
| 芯片 | ESP32-S3 |
| Flash | 16 MB |
| ESP-IDF | v5.5.3 (`2c211b236707889e8400c4dc5644dd5c4ee071e0`) |
| 构建日期 | 2026-08-17 |
| 实机验证 | 通过 |

验证项目：启动、ST7305 显示、8 MB PSRAM、PCF85063、SHTC3、USB 串口校时、
`Hello, world.`。详细信息见[实机验证记录](../../docs/bringup-log.md)。

在仓库根目录执行：

```bash
cd dist/v0.1.1
sha256sum --check SHA256SUMS
cd ../..
./scripts/flash.sh --port COM5 \
  --firmware dist/v0.1.1/esp32-rlcd-firmware-v0.1.1.bin \
  --confirm
```

项目许可证、第三方组件和字体的完整声明见 [`LICENSE`](../../LICENSE)、
[`NOTICE.md`](../../NOTICE.md) 与 [`LICENSES/`](../../LICENSES/)。再分发此固件时必须
同时保留这些材料。
