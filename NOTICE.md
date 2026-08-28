# 第三方软件与字体声明

ESP32 RLCD Firmware
Copyright 2026 taifu

本项目自行编写的源码、脚本和文档采用 Apache License 2.0，完整条款见
[`LICENSE`](LICENSE)。发布固件还包含下列第三方软件和字体；它们继续适用各自的
许可证。本文件提供来源、固定版本和许可索引，完整许可文本保存在 [`LICENSES/`](LICENSES/)。

这些许可文本属于随二进制分发的合规材料，不是第三方源码。实际第三方源码仍由构建脚本
下载到 Git 仓库之外，仓库中不复制或维护它们。

## 构建组件

| 组件 | 固定版本或来源 | 用途 | 许可证与文本 |
| --- | --- | --- | --- |
| ESP-IDF | v5.5.3，`2c211b236707889e8400c4dc5644dd5c4ee071e0` | 芯片支持、驱动、Bootloader、NVS、HTTP/HTTPS 服务和运行时 | Apache-2.0；见 [`LICENSE`](LICENSE) |
| ESP32 Wi-Fi、PHY 与共存二进制库 | ESP-IDF v5.5.3 内含 | ESP32-S3 2.4 GHz 无线连接 | Apache-2.0；各库许可文本与 [`LICENSE`](LICENSE) 完全一致 |
| FreeRTOS Kernel | ESP-IDF v5.5.3 内含 V10.5.1 | 实时操作系统内核 | MIT；见 [`LICENSES/FreeRTOS.txt`](LICENSES/FreeRTOS.txt) |
| Mbed TLS | ESP-IDF 内含 3.6.5，`ffb280bb63c78bfec1e1ab55040671768c85c923` | HTTPS 服务器认证、固件哈希与 Wi-Fi 加密支持 | Apache-2.0 OR GPL-2.0-or-later；本固件选择 Apache-2.0，见 [`LICENSES/Mbed-TLS.txt`](LICENSES/Mbed-TLS.txt) |
| lwIP | ESP-IDF 内含 2.2.0 | TCP/IP、DHCP、DNS 与 SNTP | BSD-3-Clause；见 [`LICENSES/lwIP.txt`](LICENSES/lwIP.txt) |
| ESP-SR | v2.4.7，`2f8c4b0459db5bbb39abd77adae27962d6d94bcb` | 离线语音前端、中文命令识别和模型 | ESPRESSIF MIT 及部分 Apache-2.0 源文件；定制许可见 [`LICENSES/ESP-SR.txt`](LICENSES/ESP-SR.txt)，Apache-2.0 文本见 [`LICENSE`](LICENSE) |
| ESP-DSP | v1.8.0，`196825deaa4848b2c8e87b6126491cd7fc87e5bf` | ESP-SR 的数字信号处理依赖 | Apache-2.0；见 [`LICENSE`](LICENSE) |
| `dl_fft` | v0.6.0，`a8a7b60ea5bfd6ce46960ea061641fffa9589440` | ESP-SR 的 FFT 依赖 | 组件清单声明 MIT，部分源文件明确为 Apache-2.0；见 [`LICENSES/dl_fft-MIT.txt`](LICENSES/dl_fft-MIT.txt) 和 [`LICENSE`](LICENSE) |
| cJSON | Espressif 组件 v1.7.19，`721d625669f4e4fdfe6e02cf7e11f15b33f13e3a` | 解析在线固件、图库和 ESP-SR 模型清单 | MIT；见 [`LICENSES/cJSON.txt`](LICENSES/cJSON.txt) |
| wpa_supplicant | ESP-IDF 内含 2.10 | WPA2/WPA3 客户端认证 | BSD-3-Clause；见 [`LICENSES/WPA-Supplicant.txt`](LICENSES/WPA-Supplicant.txt) 和 [`LICENSES/WPA-Supplicant-COPYING.txt`](LICENSES/WPA-Supplicant-COPYING.txt) |
| HTTP Parser | ESP-IDF 内含 2.7.0 | 本地配网页面的 HTTP 请求解析 | MIT；见 [`LICENSES/HTTP-Parser.txt`](LICENSES/HTTP-Parser.txt) |
| Espressif QR Code | v0.2.0，`3117b1e6806738c0271bbd18df17f6d74ec66452` | 在配网页面显示标准 Wi-Fi 加入二维码 | Espressif 包装层为 Apache-2.0，见 [`LICENSE`](LICENSE)；内含 Nayuki QR Code Generator，MIT，见 [`LICENSES/Nayuki-QR-Code-Generator.txt`](LICENSES/Nayuki-QR-Code-Generator.txt) |
| Espressif `esp_codec_dev` | v1.3.5，ESP-ADF `9b35bca1a6db3d989936f228d6e28f33089fa9e7`，随微雪固定提交提供 | ES8311 扬声器与 ES7210 双麦克风控制 | Apache-2.0；见 [`LICENSE`](LICENSE) |
| newlib | Espressif Xtensa 工具链 `esp-14.2.0_20251107` | C 标准库 | 多个宽松许可证；见 [`LICENSES/Newlib.txt`](LICENSES/Newlib.txt) |
| GCC runtime（`libgcc`、`libstdc++`） | GCC 14.2.0，Espressif Xtensa 工具链 | 编译器运行时和 C++ 运行库 | GPL-3.0-or-later WITH GCC-exception-3.1；见 [`GPL-3.0-or-later.txt`](LICENSES/GPL-3.0-or-later.txt) 和 [`GCC-Runtime-Library-Exception-3.1.txt`](LICENSES/GCC-Runtime-Library-Exception-3.1.txt) |
| TLSF | ESP-IDF 内含 `2867f6883a12920b1969ff9624c0ab0e4185c2ce` | 堆内存分配器 | BSD-3-Clause；见 [`LICENSES/TLSF-BSD-3-Clause.txt`](LICENSES/TLSF-BSD-3-Clause.txt) |
| Cadence/Tensilica Xtensa 支持代码 | ESP-IDF v5.5.3 / ESP32-S3 HAL | CPU 启动、中断和 HAL | MIT；见 [`LICENSES/Cadence-Xtensa-MIT.txt`](LICENSES/Cadence-Xtensa-MIT.txt) |
| U8g2 | 微雪固定提交中的组件 | 单色图形和字体渲染 | BSD-2-Clause；见 [`LICENSES/U8g2.txt`](LICENSES/U8g2.txt) |
| `u8g2_st7305` | 微雪仓库 `eb1f63427d735a22b9c30e22fa63ebddae1834d3` | ST7305 ESP-IDF 适配 | Apache-2.0；见 [`LICENSE`](LICENSE) |

ESP-IDF 和微雪仓库中未另行标注的 Apache-2.0 代码，均使用根目录的同一份 Apache-2.0
完整文本。ESP-IDF 内部只有实际进入当前源码链接映像的独立许可组件列在上表中；正式
Release 同时附带该版本打包时的本文件与完整许可文本。

主要上游版权归属包括：

- ESP-IDF：Espressif Systems (Shanghai) CO LTD 及贡献者；
- FreeRTOS Kernel：Copyright (C) 2021 Amazon.com, Inc. or its affiliates；
- Mbed TLS：Copyright The Mbed TLS Contributors；
- lwIP：Copyright (c) 2001, 2002 Swedish Institute of Computer Science；
- wpa_supplicant：Copyright (c) 2002-2022, Jouni Malinen 及贡献者；
- HTTP Parser：基于 Igor Sysoev 的 NGINX 解析器，并包含 Joyent, Inc. 及 Node 贡献者的
  修改；
- ESP-SR：Copyright (c) 2018 ESPRESSIF SYSTEMS (SHANGHAI) PTE LTD；其定制许可仅允许
  在 ESPRESSIF SYSTEMS 产品上免费使用，本项目目标硬件采用 ESP32-S3；
- ESP-DSP 与 `dl_fft`：Espressif Systems (Shanghai) PTE LTD、Espressif Systems
  (Shanghai) CO LTD 及贡献者；
- cJSON：Copyright (c) 2009-2017 Dave Gamble and cJSON contributors；
- Espressif QR Code：Copyright 2015-2021 Espressif Systems (Shanghai) CO LTD；底层
  QR Code Generator：Copyright (c) Project Nayuki；
- `esp_codec_dev`：Copyright 2023 Espressif Systems (Shanghai) CO LTD；
- U8g2：Copyright (c) 2016, olikraus@gmail.com；
- `u8g2_st7305`：Copyright 2026 Waveshare；
- TLSF、Cadence/Tensilica、newlib、GCC runtime 和各字体的完整版权声明，保留在上表链接的
  许可材料中。

## 内嵌字体

当前源码使用以下 U8g2 字体。字体数据编译进固件，但字体版权不转为本项目所有：

| 字体 | 许可证与说明 |
| --- | --- |
| `u8g2_font_5x8_tf`、`u8g2_font_6x13_tf` | Public domain；上游字体元数据声明为“Public domain font” |
| `u8g2_font_helvB14_tf`、`u8g2_font_helvB18_tf`、`u8g2_font_helvB24_tf` | Adobe/DEC X11 字体条款；完整版权和许可见 [`LICENSES/U8g2.txt`](LICENSES/U8g2.txt) |
| `u8g2_font_wqy16_t_gb2312` | GPL-2.0-only WITH Font-exception-2.0；见 [`WenQuanYi-Bitmap-Song.txt`](LICENSES/WenQuanYi-Bitmap-Song.txt) 和 [`GPL-2.0-only.txt`](LICENSES/GPL-2.0-only.txt) |
| `u8g2_font_logisoso20_tf`、`u8g2_font_logisoso78_tn` | 按字体归档中的 `COPYING.TXT` 采用 GPL-2.0-only WITH Font-exception-2.0；见 [`Logisoso.txt`](LICENSES/Logisoso.txt) 和 [`GPL-2.0-only.txt`](LICENSES/GPL-2.0-only.txt) |

文泉驿和 Logisoso 字体均以未修改字形的形式嵌入。各自的字体例外原文说明：在文档中
嵌入字体或未修改字体片段，不会仅因该字体而使文档适用 GPL；本分发包同时保留例外声明
和 GPL v2 完整文本。

## 来源

依赖版本和下载地址固定在 [`tool-versions.env`](tool-versions.env)。构建目录选择规则及
更新许可材料的检查步骤见[开发与发布流程](docs/development.md)。
