# 开发与发布流程

## 技术栈

- 只使用原生 ESP-IDF，不维护 Arduino 构建路线。
- 当前固定 ESP-IDF v5.5.3，目标芯片为 ESP32-S3。
- 项目源码按 ESP-IDF 组件拆分在 `src/`，应用入口位于 `src/app/`。
- 网络功能独立位于 `src/network/`，使用 ESP-IDF Wi-Fi、HTTP Server、NVS 与
  ESP-NETIF SNTP API。
- U8g2、微雪 ST7305 适配器和 Espressif QR Code 作为外部构建依赖使用。

固定版本见 `tool-versions.env`。如果需要升级依赖，必须同时更新版本/提交哈希、重新完整
构建、实机验收并记录到 CHANGELOG。

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
./scripts/set-rtc.sh --port COM5
```

具体步骤见[开发烧录指南](flashing.md)，不从聊天记录还原命令。

`configure.sh` 调用固定版本 ESP-IDF 的 `menuconfig`，修改写入已忽略的本地 `sdkconfig`。
项目可公开的默认值保存在 `sdkconfig.defaults`；个人固定热点密码不得写入该文件或提交到
Git。删除 `sdkconfig` 后会恢复项目默认值。

`package-release.sh` 在 `build/release/vX.Y.Z/` 生成待上传文件，包括固件、校验值、
`LICENSE`、`NOTICE.md` 和完整许可文本压缩包，并在返回成功前自检全部附件。它不会创建
或修改 GitHub Release，也不会更改仓库可见性。

## 版本发布清单

1. 在 `CMakeLists.txt` 更新 `PROJECT_VER`。
2. 在 `CHANGELOG.md` 把变化写入对应版本。
3. 执行 `./scripts/bootstrap.sh --check`。
4. 执行 `./scripts/check-repository.sh`，确认仓库边界、文档链接和历史发布哈希正常。
5. 执行 `./scripts/check-licenses.sh`；新增或升级组件、字体时先更新 `NOTICE.md`、
   `LICENSES/` 和检查脚本。
6. 执行 `./scripts/test.sh`，确认纯逻辑测试通过。
7. 执行 `./scripts/build.sh`，确认无编译错误。
8. 核对 `build/SHA256SUMS`。
9. 按[开发烧录指南](flashing.md)烧录到实机。
10. 验证屏幕、RTC、农历、温湿度、电池 ADC、PSRAM 和串口日志。
    含联网功能的版本还必须验证首次配网、错误密码恢复、自动 SNTP、RTC 写后回读、断电
    重连、`GET_NETWORK`、USB `RESET_WIFI`、`KEY` 状态页和长按重配网，并确认日志不
    包含家庭 SSID 或密码。
11. 只有实机通过后，才把合并镜像复制到 `dist/vX.Y.Z/`，并保存大小、SHA-256、
    构建依赖和验收记录。
12. 按[发布固件安装指南](user-install.md)重新执行发布路径，确认普通用户命令和文件名一致。
13. 执行 `./scripts/package-release.sh vX.Y.Z`，检查 `build/release/vX.Y.Z/` 中的全部附件
    和 `RELEASE_SHA256SUMS`。
14. 检查 Git 暂存内容，确保没有第三方源码、`build/`、`sdkconfig`、密码或外部固件。
15. 提交并创建 `vX.Y.Z` 标签。
16. 创建 GitHub Release，上传打包目录中的完整固件、校验文件、效果图、`LICENSE`、
    `NOTICE.md` 和许可文本压缩包。
17. GitHub Releases 只保留最新正式版本；删除旧 Release 条目时保留 `dist/` 历史文件。
18. 仓库可见性是独立的人工决定；构建、打包和发布脚本均不得自动切换 Public/Private。

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
- 不改写或提交仓库外的上游参考源码。

## 网络功能验收

当前网络设计与操作说明见[自动配网与网络校时](network-time.md)。正式发布前至少覆盖：

1. 空白 NVS 启动后屏幕显示默认热点名称、随机密码和固定入口地址；另外验证自定义名称、
   关闭设备后缀与固定密码配置；
2. Android 和 iOS 扫描二维码均能识别热点名称和密码；特殊字符转义正确，二维码四周
   静区完整，扫码后也可按文字信息手动连接；
3. 手机连接临时热点，提交开放网络和 WPA2/WPA3 Personal 凭据的正常/错误边界；
4. 正确凭据下取得 DHCP 地址，SNTP 获取 UTC+8 时间，PCF85063 写入后秒数持续增加；
5. 家庭 Wi-Fi 凭据错误或热点离线时，5 分钟修正窗口和后续重试符合文档；
6. 无 RTC 备用电池彻底断电后，NVS 凭据保留且开机能自动恢复时间；
7. USB `RESET_WIFI` 与 `KEY` 长按 5 秒都只清除项目网络命名空间，重启后回到首次配网；
   `KEY` 在 5 秒前松开必须取消，且一次长按只能触发一次；
8. 正常运行时短按 `BOOT` 能依次显示月历、固件信息并返回首屏，30 秒超时和从状态页
   继续切换均符合页面规范；按住 `BOOT` 2 秒只触发一次即时校时；
9. 固件信息页二维码能由 Android 和 iOS 扫描并打开项目最新 Release，关机后按住
   `BOOT` 再按 `PWR` 仍能正常进入 ROM 下载模式；
10. 标准完整镜像重新烧录后 NVS 被清空，并能重新完成首次配网；
11. 串口和构建产物的字符串检查不包含实机家庭 SSID、家庭密码或二维码中的临时热点密码。

纯逻辑表单测试由 `./scripts/test.sh` 执行。网络驱动、DHCP、HTTP、SNTP、NVS 和 RTC
联动无法由 WSL 主机测试替代，必须在目标开发板上验收。
