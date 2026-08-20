# ESP32 RLCD Firmware v0.6.0

这是由本项目源码及固定开源依赖构建并经过实机验收的 v0.6.0 正式发布物。

| 首屏 | 月历 |
| :---: | :---: |
| ![ESP32 RLCD Firmware v0.6.0 首屏效果图](home-screen.svg) | ![ESP32 RLCD Firmware v0.6.0 月历页效果图](calendar-screen.svg) |
| 设备健康 | 网络与时间 |
| ![ESP32 RLCD Firmware v0.6.0 设备健康页效果图](device-health.svg) | ![ESP32 RLCD Firmware v0.6.0 网络与时间页效果图](network-time.svg) |
| Wi-Fi 维护 | 关于与更新 |
| ![ESP32 RLCD Firmware v0.6.0 Wi-Fi 维护页效果图](wifi-maintenance.svg) | ![ESP32 RLCD Firmware v0.6.0 关于与更新页效果图](about-update.svg) |

> 效果图按 400 × 300 实机布局和代表性数据绘制，采用黑色背景、白色像素。全反射屏的
> 实际观感会随环境光变化。

| 项目 | 值 |
| --- | --- |
| 文件 | `esp32-rlcd-firmware-v0.6.0.bin` |
| 烧录地址 | `0x0` |
| 大小 | 1,342,944 bytes |
| SHA-256 | `85db4c11df52da261a2763245e90d5d3eb7a42a1e65606d2bd1433dc134a4820` |
| 应用镜像 SHA-256 | `c687406337fa83f6b24e108e01a5811b1ce2992aeebebe1ef8f4f6633a9a1c68` |
| 芯片 | ESP32-S3 |
| Flash | 16 MB |
| ESP-IDF | v5.5.3 (`2c211b236707889e8400c4dc5644dd5c4ee071e0`) |
| 构建日期 | 2026-08-20 |
| 功能与布局实机验收 | 通过 |

本版本把页面重新组织为两条稳定路径：`BOOT` 在首屏与月历之间切换，`KEY` 进入并循环
设备健康、网络与时间、Wi-Fi 维护、关于与更新。即时校时和清除网络配置只在对应系统页
长按 `KEY` 时生效；其他页面没有隐藏长按动作。

完整镜像会覆盖 NVS，因此安装后需重新配网。在仓库根目录执行：

```bash
cd dist/v0.6.0
sha256sum --check SHA256SUMS
cd ../..
./scripts/flash.sh --port COM5 \
  --firmware dist/v0.6.0/esp32-rlcd-firmware-v0.6.0.bin \
  --confirm
```

烧录后正常开机，按屏幕二维码连接临时热点；配网页面没有自动打开时访问
`http://192.168.4.1`。完整步骤见[发布固件安装指南](../../docs/user-install.md)，验证结果见
[实机验证记录](../../docs/bringup-log.md)。项目许可证、第三方组件和字体的完整声明见
[`LICENSE`](../../LICENSE)、[`NOTICE.md`](../../NOTICE.md) 与
[`LICENSES/`](../../LICENSES/)；再分发此固件时必须同时保留这些材料。
