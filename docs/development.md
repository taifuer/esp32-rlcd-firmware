# 开发与发布流程

## 技术栈

- 只使用原生 ESP-IDF，不维护 Arduino 构建路线。
- 当前固定 ESP-IDF v5.5.3，目标芯片为 ESP32-S3。
- 项目源码按 ESP-IDF 组件拆分在 `src/`，应用入口位于 `src/app/`。
- U8g2 与微雪 ST7305 适配器作为外部构建依赖使用。

固定版本见 `tool-versions.env`。如果需要升级依赖，必须同时更新版本/提交哈希、重新完整
构建、实机验收并记录到 CHANGELOG。

## 依赖与版本控制

版本库只保存项目源码、文档、辅助脚本，以及经过实机验证的自研发布固件。以下内容
由开发环境按需准备，不提交到版本库：

- ESP-IDF、编译器工具链和 Python 环境；
- U8g2、微雪示例等第三方源码；
- `build/`、`sdkconfig` 等本机构建结果；
- 社区固件和其他外部预编译文件。

第三方组件的来源和许可说明记录在 `NOTICE.md`。发布固件只有在源码构建和实机验收
均通过后，才能加入 `dist/vX.Y.Z/`。

### 外部依赖目录

脚本按以下顺序选择依赖根目录：

1. 环境变量 `RLCD_DEPS_DIR`；
2. 当前 WSL 工作区的 `../../third_party/`；
3. 独立克隆仓库旁的 `../esp32-rlcd-firmware-deps/`。

`bootstrap.sh` 只写这个外部目录，不会把依赖复制到版本库中。

## 常用命令

```bash
./scripts/bootstrap.sh --check
./scripts/build.sh
./scripts/flash.sh --port COM5 --confirm
./scripts/set-rtc.sh --port COM5
```

烧录细节以 `docs/flashing.md` 为准，不从聊天记录还原命令。

## 版本发布清单

1. 在 `CMakeLists.txt` 更新 `PROJECT_VER`。
2. 在 `CHANGELOG.md` 把变化写入对应版本。
3. 执行 `./scripts/bootstrap.sh --check`。
4. 执行 `./scripts/build.sh`，确认无编译错误。
5. 核对 `build/SHA256SUMS`。
6. 按 `docs/flashing.md` 烧录到实机。
7. 验证屏幕、RTC、传感器、PSRAM 和串口日志。
8. 只有实机通过后，才把合并镜像复制到 `dist/vX.Y.Z/`，并保存大小、SHA-256、
   构建依赖和验收记录。
9. 检查 Git 暂存内容，确保没有 `third_party/`、`build/`、`sdkconfig`、密码或外部固件。
10. 提交并按需要创建 `vX.Y.Z` 标签。

## Git 身份和 Agent 提交规则

本仓库使用：

```text
user.name  = taifu
user.email = taifu@taifua.com
```

Agent 协助产生的每个提交必须在提交正文包含：

```text
Co-Authored-By: Codex (GPT-5.6 Sol) <noreply@openai.com>
```

提交前使用 `git diff --cached --check` 和 `git status --short` 检查内容。

## 安全约定

- 构建与烧录是两个明确分开的动作。
- 烧录必须显式传入 `--confirm`，且脚本必须验证目标 VID/PID。
- 不默认执行 `erase-flash`。
- Wi-Fi 密码和其他私密配置只能放在被忽略的本地文件中。
- 不改写或提交仓库外的上游参考源码。
