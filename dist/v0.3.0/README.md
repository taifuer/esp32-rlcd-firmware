# ESP32 RLCD Firmware v0.3.0

这是由本项目源码及固定开源依赖构建的 v0.3.0 正式发布物，不是对外部或社区完整固件的
改名或重新打包。

![ESP32 RLCD Firmware v0.3.0 首屏效果图](home-screen.svg)

> 按 v0.3.0 的 400 × 300 实机布局和代表性数据绘制；图中按实机的黑色背景、白色像素
> 显示极性呈现。全反射屏的实际观感会随环境光变化。

| 项目 | 值 |
| --- | --- |
| 文件 | `esp32-rlcd-firmware-v0.3.0.bin` |
| 效果图 | `home-screen.svg` |
| 烧录地址 | `0x0` |
| 大小 | 1,328,928 bytes |
| SHA-256 | `0b9705797c3f36e7e1cb84b654daad74fbeecd6d5800672c244c7d4d2ba139c8` |
| 应用镜像 SHA-256 | `f2754906541c41e7f751d3dd3ef58460868435c36c6ed1392e8647d20d852c9c` |
| 芯片 | ESP32-S3 |
| Flash | 16 MB |
| ESP-IDF | v5.5.3 (`2c211b236707889e8400c4dc5644dd5c4ee071e0`) |
| 构建日期 | 2026-08-19 |
| 功能与布局实机验收 | 通过 |

本版本在 v0.2.0 离线仪表盘基础上加入本地 Wi-Fi 配网、随机密码临时热点、Wi-Fi 加入
二维码、NVS 凭据保存、SNTP 自动校时、24 小时重校、首屏 Wi-Fi 状态和网络维护 USB
命令。小米 Android 手机扫码、首次配网、家庭 2.4 GHz Wi-Fi、网络校时、RTC 写入和首屏
布局均已在实机验收；正式版本构建另行通过许可检查、全部主机测试和 ESP-IDF 完整构建。

完整镜像会覆盖 NVS，因此安装后需重新配网。在仓库根目录执行：

```bash
cd dist/v0.3.0
sha256sum --check SHA256SUMS
cd ../..
./scripts/flash.sh --port COM5 \
  --firmware dist/v0.3.0/esp32-rlcd-firmware-v0.3.0.bin \
  --confirm
```

烧录后正常开机，按屏幕二维码连接临时热点；配网页面没有自动打开时访问
`http://192.168.4.1`。完整步骤见[发布固件安装指南](../../docs/user-install.md)，验证结果见
[实机验证记录](../../docs/bringup-log.md)。项目许可证、第三方组件和字体的完整声明见
[`LICENSE`](../../LICENSE)、[`NOTICE.md`](../../NOTICE.md) 与
[`LICENSES/`](../../LICENSES/)；再分发此固件时必须同时保留这些材料。
