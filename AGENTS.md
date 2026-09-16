# Repository instructions

- 用户已确认实施、且需要实机测试的固件改动，在完成测试和构建后，应使用固定流程准备
  Beta OTA，并校验公网清单与下载文件，再通知用户测试（2026-09-14 用户追加要求）。
  用户明确要求仅本地、暂停或暂不部署时，以该要求为准；纯调研和文档修改不触发部署。
- Git 提交、推送和正式发布仍需用户明确确认；准备 Beta OTA 不等于授权这些动作，
  不以尚未发布为由自动推送开发代码，也不自动通过 USB 烧录设备。

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
  Co-Authored-By: Codex (GPT‑6 Astra) <noreply@openai.com>
  ```
