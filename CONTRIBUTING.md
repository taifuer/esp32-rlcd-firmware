# 参与贡献

感谢关注 ESP32 RLCD Firmware。本项目当前只支持 Waveshare ESP32-S3-RLCD-4.2，使用
原生 ESP-IDF，不维护 Arduino 构建路线。

## 提交改动前

较大的功能或硬件变更请先创建 Issue，说明使用场景、硬件版本和预期行为。修正文档、
测试或明确的小问题可以直接提交 Pull Request。

开发依赖保存在仓库之外。准备环境并执行完整检查：

```bash
./scripts/bootstrap.sh
./scripts/check-repository.sh
./scripts/check-licenses.sh
./scripts/test.sh
./scripts/build.sh
```

涉及显示、RTC、传感器、电池或电源行为的改动，还需要在目标开发板上验收，并在 Pull
Request 中说明硬件、烧录方式和验证结果。

## 仓库内容

请使用自己的 Git 身份提交，并确保：

- 不提交 ESP-IDF、U8g2、微雪示例或其他第三方源码；
- 不提交 `build/`、本机配置、密钥、Wi-Fi 凭据或外部预编译固件；
- 新增第三方组件或字体时，同步更新 `NOTICE.md`、`LICENSES/` 和许可检查脚本；
- 显著变更更新 `CHANGELOG.md`，发布文件遵循 `docs/development.md` 的清单；
- 提交前执行 `git diff --check`，保持改动聚焦并说明验证方式。

提交贡献即表示你有权提交相关内容，并同意项目按照 Apache License 2.0 分发你的贡献。
