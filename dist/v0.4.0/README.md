# ESP32 RLCD Firmware v0.4.0

这是由本项目源码及固定开源依赖构建并经过实机验收的 v0.4.0 正式发布物。

![ESP32 RLCD Firmware v0.4.0 首屏效果图](home-screen.svg)

![ESP32 RLCD Firmware v0.4.0 设备状态页效果图](device-status.svg)

> 效果图按 400 × 300 实机布局和代表性数据绘制，采用黑色背景、白色像素。全反射屏的
> 实际观感会随环境光变化。

| 项目 | 值 |
| --- | --- |
| 文件 | `esp32-rlcd-firmware-v0.4.0.bin` |
| 烧录地址 | `0x0` |
| 大小 | 1,331,936 bytes |
| SHA-256 | `315ddf545ad328ba85326e526e290dcdc30e9b1e95a38fdf55052dfd8006c808` |
| 应用镜像 SHA-256 | `f7be07fe255e9cadcfb356fe13be95cbc60f7679826e3046f027c17171e71c74` |
| 芯片 | ESP32-S3 |
| Flash | 16 MB |
| ESP-IDF | v5.5.3 (`2c211b236707889e8400c4dc5644dd5c4ee071e0`) |
| 构建日期 | 2026-08-20 |
| 功能与布局实机验收 | 通过 |

本版本加入板载 `KEY` 维护操作：短按查看固件、RTC、传感器、电池、网络和最近校时状态，
再次短按或等待 15 秒返回首屏；按住 1 秒后显示重配网倒计时，持续满 5 秒才会清除项目
Wi-Fi 凭据并重启，中途松开不会修改配置。短按、自动返回、取消、长按清除、重新配网和
SNTP 校时均已在实机验收。

完整镜像会覆盖 NVS，因此安装后需重新配网。在仓库根目录执行：

```bash
cd dist/v0.4.0
sha256sum --check SHA256SUMS
cd ../..
./scripts/flash.sh --port COM5 \
  --firmware dist/v0.4.0/esp32-rlcd-firmware-v0.4.0.bin \
  --confirm
```

烧录后正常开机，按屏幕二维码连接临时热点；配网页面没有自动打开时访问
`http://192.168.4.1`。完整步骤见[发布固件安装指南](../../docs/user-install.md)，验证结果见
[实机验证记录](../../docs/bringup-log.md)。项目许可证、第三方组件和字体的完整声明见
[`LICENSE`](../../LICENSE)、[`NOTICE.md`](../../NOTICE.md) 与
[`LICENSES/`](../../LICENSES/)；再分发此固件时必须同时保留这些材料。
