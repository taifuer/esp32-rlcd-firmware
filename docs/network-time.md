# 自动配网与网络校时

本文说明 v0.4.0 的本地配网与联网校时功能。主机测试、ESP-IDF 完整构建、开发板首次
配网、SNTP 校时和小米手机二维码扫码均已验收；安装步骤见
[发布固件安装指南](user-install.md)。

## 工作流程

```text
未保存 Wi-Fi ──> 临时加密热点与配网页面 ──> 凭据写入 NVS
                                              │
                                              v
RTC 无效或到达重校周期 <── 写入 PCF85063 <── SNTP 校时 <── 连接家庭 Wi-Fi
                                              │
                                              v
                                      关闭 Wi-Fi，24 小时后重校
```

首次启动且设备中没有网络配置时：

1. 屏幕显示 Wi-Fi 加入二维码、临时热点名称、随机密码和 `http://192.168.4.1`；热点名称形如
   `ESP32-RLCD-A1B2C3`。默认密码是 8 位大写字母与数字，排除容易混淆的
   `0/O/1/I`，并在每次开启配网模式时重新生成。
2. 用支持标准 Wi-Fi 二维码的系统相机扫描并确认加入该 WPA2 热点；也可以按屏幕文字
   手动连接。小米 Android 手机已实机验证，其他机型取决于系统扫码器实现。系统可能
   自动弹出配网页面；没有弹出时，在浏览器中手动打开
   `http://192.168.4.1`。
3. 填写设备要连接的 2.4 GHz Wi-Fi 名称和密码，然后选择“保存并校时”。
4. 设备关闭临时热点，连接家庭 Wi-Fi，并配置 `ntp.aliyun.com`、
   `cn.pool.ntp.org` 和 `pool.ntp.org`。
5. 校时成功后，设备按中国标准时间（UTC+8、无夏令时）写入 PCF85063 RTC，并关闭
   Wi-Fi；运行期间每 24 小时重新同步一次。

没有连接独立 RTC 备用电池时，彻底断电会使 PCF85063 时间失效。网络凭据仍保存在 Flash
中，因此再次开机后固件会自动联网并恢复时间，不再需要每次连接 USB 手动校时。

支持开放网络以及 WPA2/WPA3 Personal 网络，不支持 WEP 和企业认证网络。ESP32-S3 只在
2.4 GHz 频段工作；仅提供 5 GHz 的热点无法连接。

### 配置临时热点

临时配网热点使用 ESP-IDF 原生 Kconfig，不在 `network_time.c` 中硬编码项目参数。运行：

```bash
./scripts/configure.sh
```

然后进入 `Component config -> ESP32 RLCD network`：

| 选项 | 项目默认值 | 说明 |
| --- | --- | --- |
| Setup access point base name | `ESP32-RLCD` | 临时热点基础名称 |
| Append a unique device ID | 开启 | 追加 `-A1B2C3` 形式的设备编号，附近有多台设备时仍可区分 |
| Fixed setup access point password | 空 | 空值表示每次生成新的 8 位随机密码；也可设置 8—63 位可打印 ASCII 固定密码 |

`sdkconfig.defaults` 保存可以公开的项目默认值；`configure.sh` 生成的本地 `sdkconfig` 已被
Git 忽略。不要直接手改生成文件，也不要把个人固定密码写进 `sdkconfig.defaults`。固定
密码会嵌入应用镜像，适合受控部署，但拿到固件的人可能提取它；日常使用建议保留空值和
随机密码。

这些选项只控制 ESP32 为配网临时创建的热点。设备最终连接的家庭 Wi-Fi 名称和密码仍在
浏览器页面中填写并写入 NVS，不属于编译配置。

### 二维码行为

二维码内容使用兼容 ZXing、Android 和 iOS 11+ 的 Wi-Fi 配置格式：

```text
WIFI:T:WPA;S:热点名称;P:热点密码;;
```

SSID 和密码中的反斜杠、分号、逗号、双引号与冒号会按格式要求转义。二维码使用中等级别
纠错和四模块静区，并根据实际版本在屏幕上选择整数像素倍率，避免插值造成边缘模糊。
字段值不会被额外包在双引号中；部分 Android 厂商解析器会把这种双引号当作 SSID 或密码
本身，继而连接到错误的网络名称。
扫码只负责加入临时热点；配网页面仍由 DHCP Captive Portal 提示或
`http://192.168.4.1` 打开，因此二维码不依赖公网 URL。

```text
┌────────────────────────────────────────┐
│              WI-FI SETUP               │
├────────────────────────────────────────┤
│                █▀▀▀█                   │
│                █ QR█                   │
│                ▀▀▀▀▀                   │
│ SSID: ESP32-RLCD-A1B2C3                │
│ PASS: ABCDEFG2                         │
│ OPEN: http://192.168.4.1               │
└────────────────────────────────────────┘
```

### 为什么使用 192.168.4.1

`192.168.4.1` 是 ESP-IDF 默认 SoftAP 网络接口使用的私有网关地址，当前固件没有改写
这个默认值，因此配网页面地址是固定的。临时热点形成的是设备与手机之间独立的
`192.168.4.0/24` 局域网，不要求与家庭路由器处于同一网段。

没有使用更常见的 `192.168.1.1`，可以降低它与家庭路由器、公司网络或手机现有路由发生
地址冲突的概率。地址本身不代表公网服务器，也不会把配网信息发送到互联网；如果未来
修改 SoftAP 网段，屏幕、DHCP Option 114 和网页入口必须同时修改。

## 屏幕行为

- RTC 无效时，屏幕持续显示当前连接、校时或配网状态，直到获得有效时间；
- RTC 已有效但需要配网时，先显示仪表盘 3 秒，再显示配网信息 60 秒，随后恢复仪表盘；
- 配网页面保持可用的时间可能长于屏幕提示时间，需要重新查看热点密码时可重启设备；
- 配网和错误页是静态画面，固件不会每秒重复刷新全反射屏。

已保存的网络连接失败时，固件最多等待约 40 秒，然后开放临时配网热点 5 分钟，以便修改
凭据；无人操作时关闭热点、等待 5 分钟后再尝试连接。网络故障不会删除已有 RTC 时间，
USB 手动校时也始终保留为后备方式。

需要主动更换家庭 Wi-Fi 时，按住板载 `KEY`：1 秒后屏幕显示倒计时，满 5 秒后只清除
`rlcd_net` 命名空间并重启进入首次配网。倒计时结束前松开会取消操作。该功能与 USB
`RESET_WIFI` 的清除范围相同，不会擦除整片 Flash；`BOOT` 只用于进入 ROM 下载模式。

## USB 命令

正常启动后，USB Serial/JTAG 支持以下逐行命令：

```text
HELP
GET_TIME
SET_TIME YYYY-MM-DD HH:MM:SS
GET_NETWORK
RESET_WIFI
```

- `GET_NETWORK` 只返回状态、是否已配置及最近错误，不输出家庭 Wi-Fi 名称或密码；
- `RESET_WIFI` 只清除本项目的网络凭据命名空间，然后重启进入配网模式；
- `SET_TIME` 继续使用现有日期校验、星期计算和写后回读流程。

日常手动校时仍可执行：

```bash
./scripts/set-rtc.sh --port COM5
```

## 凭据与安全边界

- 家庭 Wi-Fi 名称和密码只写入设备的 `rlcd_net` NVS 命名空间；源码、构建目录、串口日志
  和 Git 仓库均不保存它们；
- 当前分区未启用 NVS 加密，因此能够物理读取 Flash 的攻击者可能取得网络凭据；这与面向
  低风险家庭环境的初始版本定位一致，不应把设备交给不可信人员；
- 配网页面使用 HTTP，但只在临时 WPA2 热点内提供。它不依赖云服务、脚本、外部字体或
  第三方网页；建议在设备附近完成配置；
- 保持默认空配置时，临时热点密码在 Wi-Fi RF 启用后由硬件 RNG 生成，8 个字符来自
  32 字符集合，对应 40 位随机量；改用固定密码时，该密码会存在本地 `sdkconfig` 和
  固件镜像中。两种模式都不会把密码写入串口日志；
- 上游二维码包装层原本会在 INFO 日志输出待编码文本；本项目在编译该组件时关闭其日志，
  防止二维码中的临时热点密码进入串口输出。二维码缓冲在画面生成后立即清零；
- 本项目的标准完整合并镜像从 `0x0` 写入，映像中的空白区会覆盖位于 `0x9000` 的 NVS
  分区。因此重新烧录完整固件会清除 Wi-Fi 配置，烧录后需要重新配网；
- `erase-flash` 不是正常安装或恢复步骤，除非明确诊断出整个 Flash 必须清除，否则不要
  使用。

## 实现依据

- [ESP-IDF v5.5.3 Wi-Fi 驱动指南](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32s3/api-guides/wifi.html)
- [ESP-NETIF 与 SNTP API](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32s3/api-reference/network/esp_netif.html)
- [ESP-IDF 系统时间与 SNTP](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32s3/api-reference/system/system_time.html)
- [ESP-IDF NVS Flash API](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32s3/api-reference/storage/nvs_flash.html)
- [ESP-IDF Captive Portal 示例](https://github.com/espressif/esp-idf/tree/v5.5.3/examples/protocols/http_server/captive_portal)
- [Espressif QR Code v0.2.0](https://components.espressif.com/components/espressif/qrcode/versions/0.2.0/readme)
- [ZXing Wi-Fi 二维码字段格式](https://github.com/zxing/zxing/wiki/Barcode-Contents#wi-fi-network-config-android-ios-11)

实现使用 ESP-IDF 原生 Wi-Fi、HTTP Server、NVS 与 ESP-NETIF SNTP API。配网入口采用
DHCP Option 114 提示和固定地址，不复制第三方 DNS 重定向服务源码。
