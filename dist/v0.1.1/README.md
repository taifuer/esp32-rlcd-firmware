# ESP32 RLCD Firmware v0.1.1

这是自研固件 v0.1.1 的实机验证发布物，不包含任何外部或社区预编译固件。

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
`Hello, world.`。详细日志见 [`../../docs/bringup-log.md`](../../docs/bringup-log.md)。

烧录前执行：

```bash
(cd dist/v0.1.1 && sha256sum --check SHA256SUMS)
./scripts/flash.sh --port COM5 \
  --firmware dist/v0.1.1/esp32-rlcd-firmware-v0.1.1.bin \
  --confirm
```
