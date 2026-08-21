# 固件安装与本地升级

v0.7.0 起使用双 OTA 应用槽，将首次安装、故障恢复和日常升级明确分开。普通升级不覆盖
NVS，因此不会清除家庭 Wi-Fi 配置。

## 两类发布固件

| 文件后缀 | 用途 | 写入方式 | 是否清除 Wi-Fi |
| --- | --- | --- | --- |
| `-factory.bin` | 首次安装、旧版本迁移和故障恢复 | ROM 下载模式，从 `0x0` 写入 | 是 |
| `-ota.bin` | 已安装 v0.7.0 或更新版本后的日常升级 | 设备本地升级网页 | 否 |

两种文件不能互换。Factory 镜像包含 Bootloader、分区表、初始 OTA 数据和应用；OTA 镜像
只包含应用程序。把 OTA 镜像写入 `0x0` 会导致设备无法启动。

## 从 v0.6.0 或更早版本迁移

旧版本没有双 OTA 分区，必须进行一次完整迁移：

1. 下载并校验新版本的 `-factory.bin`；
2. 按[发布固件安装指南](user-install.md)进入 ROM 下载模式，从 `0x0` 写入；
3. 正常开机并重新完成一次 Wi-Fi 配置。

这次完整写入会更新分区表并覆盖 NVS。迁移完成后，后续本地 OTA 不再要求数据线、
`BOOT`/`PWR` 组合操作或重新配网。

## 日常本地升级

1. 下载新版本的 `-ota.bin` 并核对 `SHA256SUMS`；
2. 短按 `KEY` 进入系统中心，再按到 `ABOUT & UPDATE`；
3. 按住 `KEY` 3 秒，直到屏幕显示 `FIRMWARE UPDATE`；
4. 扫描二维码加入屏幕所示临时热点；无法自动打开页面时访问
   `http://192.168.4.1`；
5. 选择 `-ota.bin` 并开始升级；
6. 保持供电，等待屏幕依次显示接收百分比、校验和重启；
7. 重启后在“关于与更新”页核对版本。

升级热点使用带设备编号的名称和每次会话重新生成的 8 位 WPA2 随机密码，只允许一台
客户端连接。等待上传 5 分钟后自动关闭；等待阶段可短按 `BOOT` 取消。开始接收固件后，
按键不再中断 Flash 写入。

## 安全与失败恢复

- 升级入口只有在设备实体按键长按后才开放，不提供永久监听的更新端口；
- 网页和固件均在临时 WPA2 热点内本地传输，不依赖云服务；
- 设备拒绝尺寸超出 OTA 槽、镜像头无效或项目名不匹配的文件；
- 新固件写入未运行的应用槽，完整校验通过后才修改下一次启动目标；
- Bootloader 回滚已启用。新版本首次启动完成显示、按键和网络服务初始化后才确认镜像，
  未确认前异常复位会回到之前可运行的槽；
- 上传失败不会改动当前启动槽，也不会清除 NVS。

当前家庭 Wi-Fi 凭据所在的 NVS 尚未启用静态加密；能够物理读取 Flash 的攻击者仍可能
获取凭据。临时升级密码不会写入日志或仓库。

## 开发者串行更新

设备已具有 v0.7.0+ 分区表，但网页 OTA 不便使用时，可在进入 ROM 下载模式后执行：

```bash
./scripts/update-app.sh --port COM5 --confirm
```

该脚本将构建出的 OTA 应用写入 `ota_0`，通过 esptool 哈希校验后只清除 8 KiB OTA 选择
数据，使 Bootloader 从 `ota_0` 启动；位于 `0x9000` 的 NVS 保持不变。分区表、Bootloader
发生变化或设备仍运行 v0.6.0 及更早版本时，必须改用 `./scripts/flash.sh` 完整安装。

## 故障排查

- **长按后显示不可用**：先完成 Wi-Fi 配置和一次网络校时，等待网络任务结束后重试；
- **手机没有自动打开网页**：保持连接升级热点，手动访问 `http://192.168.4.1`；
- **网页提示固件无效**：确认选择的是同一 Release 中的 `-ota.bin`，不是
  `-factory.bin`；
- **上传中断**：等待失败提示返回“关于与更新”，重新长按开启新会话；当前固件仍可运行；
- **新版本无法正常启动**：Bootloader 会尝试回滚。仍无法启动时使用 `-factory.bin`
  进入 ROM 下载模式恢复。

## 实现依据

- [ESP-IDF v5.5.3 OTA API](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32s3/api-reference/system/ota.html)
- [ESP-IDF v5.5.3 分区表](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32s3/api-guides/partition-tables.html)
- [ESP-IDF HTTP Server](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32s3/api-reference/protocols/esp_http_server.html)
