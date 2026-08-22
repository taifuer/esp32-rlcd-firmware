# 开发与发布流程

## 技术栈

- 只使用原生 ESP-IDF，不维护 Arduino 构建路线。
- 当前固定 ESP-IDF v5.5.3，目标芯片为 ESP32-S3。
- 项目源码按 ESP-IDF 组件拆分在 `src/`，应用入口位于 `src/app/`。
- 网络功能独立位于 `src/network/`，使用 ESP-IDF Wi-Fi、HTTP Server、NVS 与
  ESP-NETIF SNTP API。
- 固件更新独立位于 `src/update/`：在线更新使用 HTTPS 清单、ESP-IDF CA 证书包和受控的
  HTTP Client/OTA 写入流程，本地更新使用受实体按键控制的临时网页；两者共用双 OTA
  分区与回滚。
- 音频驱动与诊断位于 `src/audio/`，使用板载 ES8311、ES7210、ESP-IDF I2S 和只保存于
  PSRAM 的非阻塞临时回放状态机。
- U8g2、微雪 ST7305 适配器、Espressif QR Code 和 `esp_codec_dev` 作为外部构建依赖使用。

固定版本见 `tool-versions.env`。如果需要升级依赖，必须同时更新版本/提交哈希、重新完整
构建、实机验收并记录到 CHANGELOG。

涉及页面、提示语或实体按键的改动，应先阅读
[产品界面与交互设计规范](design-guidelines.md)，并同步更新界面效果图和主机逻辑测试。

## 依赖与版本控制

版本库只保存项目源码、文档、辅助脚本，以及由这些源码和固定开源依赖构建并经过实机
验证的发布固件。以下内容
由开发环境按需准备，不提交到版本库：

- ESP-IDF、编译器工具链和 Python 环境；
- U8g2、微雪示例、Espressif QR Code 等第三方源码；
- `build/`、`sdkconfig` 等本机构建结果；
- 社区固件和其他外部预编译文件。

第三方组件的来源、固定版本和许可索引记录在[第三方软件与字体声明](../NOTICE.md)，完整
许可文本保存在 [`LICENSES/`](../LICENSES/)。这些文件是二进制分发所需的合规文档，
不是第三方源码。发布固件只有在源码构建和实机验收均通过后，才能加入 `dist/vX.Y.Z/`。

`dist/` 按版本保存可追溯的发布文件；面向普通用户的 GitHub Releases 页面只保留最新
正式版本。自 v0.2.0 起，每个 `dist/vX.Y.Z/README.md` 还应展示该版本自己的效果图。

### 外部依赖目录

脚本按以下顺序选择依赖根目录：

1. 环境变量 `RLCD_DEPS_DIR`；
2. 当前 WSL 工作区中已存在的共享外部依赖目录；
3. 独立克隆仓库旁的 `../esp32-rlcd-firmware-deps/`。

`bootstrap.sh` 会输出最终选中的绝对路径，并且只写入该仓库外目录，不会把依赖复制到
版本库中。项目禁用 ESP-IDF Component Manager 的隐式下载；所有外部组件均由该脚本按
`tool-versions.env` 的提交哈希准备。

## 常用命令

```bash
./scripts/bootstrap.sh --check
./scripts/configure.sh
./scripts/check-repository.sh
./scripts/check-licenses.sh
./scripts/test.sh
./scripts/build.sh
./scripts/package-release.sh vX.Y.Z
./scripts/flash.sh --port COM5 --confirm
./scripts/update-app.sh --port COM5 --confirm
./scripts/set-rtc.sh --port COM5
```

具体步骤见[开发烧录指南](flashing.md)，不从聊天记录还原命令。

`configure.sh` 调用固定版本 ESP-IDF 的 `menuconfig`，修改写入已忽略的本地 `sdkconfig`。
项目可公开的默认值保存在 `sdkconfig.defaults`；个人固定热点密码不得写入该文件或提交到
Git。删除 `sdkconfig` 后会恢复项目默认值。

`package-release.sh` 在 `build/release/vX.Y.Z/` 生成待上传文件，包括固件、校验值、
`LICENSE`、`NOTICE.md` 和完整许可文本压缩包，并在返回成功前自检全部附件。它不会创建
或修改 GitHub Release，也不会更改仓库可见性。

### 构建版本与更新通道

仓库默认构建版本为 `0.10.0`。需要构建其他版本时，通过环境变量覆盖，不直接为一次
候选构建修改 `CMakeLists.txt`：

```bash
RLCD_PROJECT_VERSION=0.10.0-rc.1 ./scripts/build.sh
RLCD_PROJECT_VERSION=0.10.0 ./scripts/build.sh
```

版本必须是固件可比较的 SemVer，且不带文件名使用的前导 `v`：

- 带 `-dev`、`-rc.1` 等预发布标识的构建只读取
  `https://mcu.taifua.com/esp32-rlcd/firmware/testing.json`；
- `0.10.0` 这类正式构建只读取
  `https://mcu.taifua.com/esp32-rlcd/firmware/stable.json`。

预发布验证先上传版本化 `-ota.bin`，核对大小和 SHA-256，再更新 `testing.json`。正式
发布必须从同一份已实机验收的源码构建正式版本，重新核对产物差异、大小与 SHA-256 后
更新 `stable.json`；不要让清单提前指向尚未上传或尚未验收的文件。v0.9.0 没有在线更新
客户端，首次迁移到 v0.10.0 仍需通过 v0.9.0 本地更新或 USB 写入。

## 版本发布清单

1. 确定 SemVer；正式构建必须显式使用 `RLCD_PROJECT_VERSION=X.Y.Z`，并确认版本不含
   预发布标识，因此设备只读取 `stable.json`。
2. 在 `CHANGELOG.md` 把 `[Unreleased]` 内容整理为对应正式版本。
3. 执行 `./scripts/bootstrap.sh --check`。
4. 执行 `./scripts/check-repository.sh`，确认仓库边界、文档链接和历史发布哈希正常。
5. 执行 `./scripts/check-licenses.sh`；新增或升级组件、字体时先更新 `NOTICE.md`、
   `LICENSES/` 和检查脚本。
6. 执行 `./scripts/test.sh`，确认纯逻辑测试通过。
7. 执行 `RLCD_PROJECT_VERSION=X.Y.Z ./scripts/build.sh`，确认无编译错误且应用描述中的
   版本正确。
8. 核对 `build/SHA256SUMS`。
9. 首次迁移使用 Factory 镜像完整烧录；后续同时验证在线更新、本地更新和保留 NVS 的
   串行应用更新，具体步骤见[开发烧录指南](flashing.md)和
   [固件安装与更新](firmware-update.md)。
10. 验证屏幕、RTC、农历、温湿度、电池 ADC、PSRAM 和串口日志。
    含联网功能的版本还必须验证首次配网、错误密码恢复、自动 SNTP、RTC 写后回读、断电
    重连、`GET_NETWORK`、USB `RESET_WIFI`、系统中心页面和上下文长按操作，并确认日志
    不包含家庭 SSID 或密码。
11. 只有实机通过后，才把 Factory 与 OTA 两个镜像复制到 `dist/vX.Y.Z/`，并保存各自
    大小、SHA-256、构建依赖和验收记录。
12. 按[发布固件安装指南](user-install.md)重新执行发布路径，确认普通用户命令和文件名一致。
13. 执行 `./scripts/package-release.sh vX.Y.Z`，检查 `build/release/vX.Y.Z/` 中的全部附件
    和 `RELEASE_SHA256SUMS`。
14. 检查 Git 暂存内容，确保没有第三方源码、`build/`、`sdkconfig`、密码或外部固件。
15. 提交并创建 `vX.Y.Z` 标签。
16. 创建 GitHub Release，上传打包目录中的 Factory/OTA 固件、校验文件、效果图、
    `LICENSE`、`NOTICE.md` 和许可文本压缩包。
17. 把同一 OTA 固件发布到版本化 HTTPS 地址，核对远端大小和 SHA-256 后再原子更新
    `stable.json`；不得让正式清单指向测试文件。
18. 用上一正式版本在线检查并确认能发现新版本，但安装仍需实体按键确认。
19. GitHub Releases 只保留最新正式版本；删除旧 Release 条目时保留 `dist/` 历史文件。
20. 仓库可见性是独立的人工决定；构建、打包和发布脚本均不得自动切换 Public/Private。

## Git 身份和 Agent 提交规则

维护者在本工作区使用：

```text
user.name  = taifu
user.email = taifu@taifua.com
```

外部贡献者必须使用自己的 Git 身份，不应冒用维护者身份。由本工作区 Agent 协助产生的
维护者提交必须在提交正文包含：

```text
Co-Authored-By: Codex (GPT-5.6 Sol) <noreply@openai.com>
```

提交前使用 `git diff --cached --check` 和 `git status --short` 检查内容。

## 安全约定

- 构建与烧录是两个明确分开的动作。
- 烧录必须显式传入 `--confirm`，且脚本必须验证目标 VID/PID。
- 不默认执行 `erase-flash`。
- 家庭 Wi-Fi 密码不得写入源码、构建配置、构建日志或 Git；运行时凭据只允许由配网页面
  写入设备 NVS。临时配网热点可在已忽略的本地 `sdkconfig` 中设置固定密码，但默认应
  留空并在运行时随机生成；固定值会嵌入固件，不应视为秘密。当前 NVS 未加密，这一限制
  必须在面向用户的文档中明确说明。
- 任何配网或网络日志都不得输出家庭 SSID、密码或 HTTP 请求正文；调试状态只报告是否
  已配置、状态名、错误码和非敏感长度。
- 二维码原文包含临时热点密码；外部组件的输入日志必须在编译时关闭，`build.sh` 会检查
  最终组件归档，发现相关日志字符串时拒绝生成可用构建结果。
- 在线更新必须通过 ESP-IDF CA 证书包验证 HTTPS；清单的项目、硬件、通道、版本、下载
  主机、大小和 SHA-256 任一不匹配都不得写入或切换启动槽。安装前必须重新读取清单，
  自动流程只能检查，不能下载或安装。
- 不改写或提交仓库外的上游参考源码。

## 网络功能验收

当前网络设计与操作说明见[自动配网与网络校时](network-time.md)。正式发布前至少覆盖：

1. 空白 NVS 启动后屏幕显示默认热点名称、随机密码和固定入口地址；另外验证自定义名称、
   关闭设备后缀与固定密码配置；
2. Android 和 iOS 扫描二维码均能识别热点名称和密码；特殊字符转义正确，二维码四周
   静区完整，扫码后也可按文字信息手动连接；
3. 手机连接临时热点，提交开放网络和 WPA2/WPA3 Personal 凭据的正常/错误边界；
4. 正确凭据下取得 DHCP 地址，SNTP 获取 UTC+8 时间，PCF85063 写入后秒数持续增加；
5. 家庭 Wi-Fi 离线时首屏、RTC、月历、温湿度、按键和音频保持可用，不自动开放配网
   热点；1、5、15、60 分钟退避正确。Wi-Fi 已连接但 NTP 不可达时标记为不同失败阶段，
   同样不打断首屏；错误凭据只能通过用户明确执行重新配网来替换；
6. 新判定记录首次显示 `RTC BACKUP: UNTESTED`；安装 RTC 备用电池并真实断电后，在任何
   NTP 操作前显示 `VERIFIED`，移除或耗尽备用电池后的真实断电显示 `FAILED`；两种情况
   都保留 NVS 网络凭据并按需自动恢复时间；
7. USB `RESET_WIFI` 与“Wi-Fi 维护”页长按 `KEY` 5 秒都只清除项目网络命名空间，重启后
   回到首次配网；5 秒前松开必须取消，且一次长按只能触发一次；
8. `BOOT` 只在首屏与月历之间切换，并能从任意系统页返回首屏；`KEY` 可进入并循环六个
   系统页，所有次级页面 30 秒超时符合规范；
9. 只有“网络与时间”页长按 `KEY` 2 秒触发即时校时，只有“音频”页长按 2 秒执行
   扬声器、双麦克风和最长 5 秒临时回放测试，只有“Wi-Fi 维护”页长按 5 秒触发重置；
   音频录制/回放中短按 `KEY` 可停止、活动阶段短按 `BOOT` 可取消且清除 PSRAM 数据，
   其他页面长按 `KEY` 或运行时长按 `BOOT` 均无操作且不会误判为短按；
10. “在线更新”页按住 `KEY` 2 秒可检查；发现新版本后再按住 2 秒进入 `REVIEW`，确认页
    必须按住 3 秒才安装。自动联网只更新检查结果，不切换页面、不下载且不静默安装；
11. 预发布版只接受 `testing.json`，正式版只接受 `stable.json`；错误项目、硬件、通道、
    版本、最低版本、URL 主机、大小和 SHA-256 均被拒绝，同版本与降级也不安装；
12. 在线检查、连接和确认阶段短按 `BOOT` 可以取消；写入开始后按键不再中断。正确 OTA
    镜像显示实际百分比、校验后重启并保留 NVS/Wi-Fi；断网、截断响应和摘要不匹配仍保留
    当前启动槽；故意不确认的新镜像能由 Bootloader 回滚；
13. “本地更新”页长按 `KEY` 3 秒能开启更新热点，短按 `BOOT` 能在等待上传时安全取消；
    上传错误文件和中断传输不会改变当前启动槽，无互联网时仍可完成本地更新；
14. 关机后按住 `BOOT` 再按 `PWR` 仍能正常进入 ROM 下载模式；此时应用未运行，屏幕不
    显示写入进度；
15. Factory 镜像重新烧录后 NVS 被清空并能重新完成首次配网；`update-app.sh` 串行更新后
    NVS 和家庭 Wi-Fi 保留；
16. 空白 NVS 的配网二维码 60 秒后恢复离线首屏，临时热点在 5 分钟后关闭；RTC 无效时
    首屏稳定显示占位符，系统中心和其他本地功能仍可操作；
17. 串口和构建产物的字符串检查不包含实机家庭 SSID、家庭密码或二维码中的临时热点密码。

纯逻辑表单测试由 `./scripts/test.sh` 执行。网络驱动、DHCP、HTTP、SNTP、NVS 和 RTC
联动无法由 WSL 主机测试替代，必须在目标开发板上验收。
