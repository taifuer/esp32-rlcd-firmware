# Repository instructions

- 本项目只维护原生 ESP-IDF 路线；不要增加 Arduino 构建或提交工具链、构建目录、
  外部源码与第三方预编译固件。
- 修改页面、按键、提示语或交互状态前，必须完整阅读并遵守
  [`docs/design-guidelines.md`](docs/design-guidelines.md)。该文件是产品设计的唯一依据，
  不要在本文件重复维护另一套设计规则。
- 日常页面保持简约、清爽、明亮和一眼可读；调试、版本、网络维护与危险操作不得混入
  日常信息层级。
- 页面与按键状态尽量实现为可在主机测试的纯逻辑；提交前至少执行
  `./scripts/check-repository.sh`、`./scripts/check-licenses.sh`、`./scripts/test.sh` 和
  `./scripts/build.sh`。
- Git 作者使用 `taifu <taifu@taifua.com>`。Agent 协助的提交正文必须包含：

  ```text
  Co-Authored-By: Codex (GPT-5.6 Sol) <noreply@openai.com>
  ```
