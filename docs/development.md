# 开发与发布流程

## 技术栈

- 只使用原生 ESP-IDF，不维护 Arduino 构建路线。
- 当前固定 ESP-IDF v5.5.3，目标芯片为 ESP32-S3。
- 项目源码按 ESP-IDF 组件拆分在 `src/`，应用入口位于 `src/app/`。
- U8g2 与微雪 ST7305 适配器作为外部构建依赖使用。

固定版本见 `tool-versions.env`。如果需要升级依赖，必须同时更新版本/提交哈希、重新完整
构建、实机验收并记录到 CHANGELOG。

## 依赖与版本控制

版本库只保存项目源码、文档、辅助脚本，以及由这些源码和固定开源依赖构建并经过实机
验证的发布固件。以下内容
由开发环境按需准备，不提交到版本库：

- ESP-IDF、编译器工具链和 Python 环境；
- U8g2、微雪示例等第三方源码；
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
版本库中。

## 常用命令

```bash
./scripts/bootstrap.sh --check
./scripts/check-licenses.sh
./scripts/test.sh
./scripts/build.sh
./scripts/package-release.sh v0.2.0
./scripts/flash.sh --port COM5 --confirm
./scripts/set-rtc.sh --port COM5
```

具体步骤见[开发烧录指南](flashing.md)，不从聊天记录还原命令。

`package-release.sh` 在 `build/release/vX.Y.Z/` 生成待上传文件，包括固件、校验值、
`LICENSE`、`NOTICE.md` 和完整许可文本压缩包，并在返回成功前自检全部附件。它不会创建
或修改 GitHub Release，也不会更改仓库可见性。

## 版本发布清单

1. 在 `CMakeLists.txt` 更新 `PROJECT_VER`。
2. 在 `CHANGELOG.md` 把变化写入对应版本。
3. 执行 `./scripts/bootstrap.sh --check`。
4. 执行 `./scripts/check-licenses.sh`；新增或升级组件、字体时先更新 `NOTICE.md`、
   `LICENSES/` 和检查脚本。
5. 执行 `./scripts/test.sh`，确认纯逻辑测试通过。
6. 执行 `./scripts/build.sh`，确认无编译错误。
7. 核对 `build/SHA256SUMS`。
8. 按[开发烧录指南](flashing.md)烧录到实机。
9. 验证屏幕、RTC、农历、温湿度、电池 ADC、PSRAM 和串口日志。
10. 只有实机通过后，才把合并镜像复制到 `dist/vX.Y.Z/`，并保存大小、SHA-256、
    构建依赖和验收记录。
11. 按[发布固件安装指南](user-install.md)重新执行发布路径，确认普通用户命令和文件名一致。
12. 执行 `./scripts/package-release.sh vX.Y.Z`，检查 `build/release/vX.Y.Z/` 中的全部附件
    和 `RELEASE_SHA256SUMS`。
13. 检查 Git 暂存内容，确保没有第三方源码、`build/`、`sdkconfig`、密码或外部固件。
14. 提交并创建 `vX.Y.Z` 标签。
15. 创建 GitHub Release，上传打包目录中的完整固件、校验文件、效果图、`LICENSE`、
    `NOTICE.md` 和许可文本压缩包。
16. GitHub Releases 只保留最新正式版本；删除旧 Release 条目时保留 `dist/` 历史文件。
17. 仓库可见性是独立的人工决定；构建、打包和发布脚本均不得自动切换 Public/Private。

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
- Wi-Fi 密码和其他私密配置只能放在被忽略的本地文件中。
- 不改写或提交仓库外的上游参考源码。
