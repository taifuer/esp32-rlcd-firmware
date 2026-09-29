# AI 对话模型评估

本文记录 AI 对话模型的选型与迁移。上游能力、价格或协议变化后需要重新核对。

## 2026-09-29：音色与回答风格（v0.32.0 开发版）

本版保持 Audio 3.0 Flash 和现有 PTT 半双工流程，不增加第二家服务商。
核对[客户端事件](https://help.aliyun.com/zh/model-studio/fun-audiochat-client-events)与
[音色配置](https://help.aliyun.com/zh/model-studio/qwen-audio-realtime-user-guides)后，
门户提供五个受控音色：`longanqian`、`longanlingxin`、`longanlingxi`、`longanxiaoxin`、
`longanlufeng`。官方实时语音文档仅列这些音色 ID，因此不根据拼音编造中文名称。

默认音色仍为 `longanqian`，默认提示保持原样。新增“英语练习”预设通过 `instructions`
要求简短英语交流、按需中文说明与少量纠错，不声称支持发音评分或硬性回答长度控制。
只在首次 `session.update` 发送选定音色，不发送 3.1 专属的输入/输出语言字段，不增加
`max_tokens`、工具调用、联网搜索或自定义提示词。

NVS 配置与记录升级到 schema/format 3，记录从 409 字节增至 411 字节。解码器继续校验
schema/format 2 的原布局与 CRC，选出最新有效槽后补上原默认选项，再写另一槽；API Key、
Host 和启用状态不变。写入或提交失败保留来源槽并在 RAM 中使用已迁移配置，下次保存或
启动重试。旧界面未提交新字段时保留当前选项，不会意外重置音色和回答风格。

主机验证覆盖五种音色和两种风格的协议 JSON、无效枚举拒绝、局部更新、密钥不回显、旧
schema 的读写/提交失败、重启恢复，以及真实 client 的首个会话设置发送路径。均为本地
测试，没有联网或录音；音色实际听感、英语练习效果和目标板连续会话仍需用户主动验收。

## 2026-09-28：旧模型下线与 v0.31.0 迁移

官方[下线通知](https://platform.qianwenai.com/docs/changelog/model-deprecations/old-models)
明确列出 `qwen3-omni-flash-realtime` 及其 2025-12-01 快照，计划于北京时间
2026-10-10 00:00 下线。换到该快照不能规避下线；本地 MultiNet 不受影响。

v0.31.0 改用固件已接入的 `qwen-audio-3.0-realtime-flash`，移除旧模型选项，
并迁移已保存配置。选择它是为了复用现有音频协议与受控交互，**不是已经通过了新的
实机 A/B 测试**；验证清单见 [v0.31.0 计划](v0.31-plan.md)。旧 Key、Host 与开关保留，
停用状态不会变成启用，也不自动跨地域重试或发起收费测试。

本次核对 [Audio 3 模型信息](https://help.aliyun.com/zh/model-studio/qwen-audio-3-0-realtime-flash)，
北京公开原价为音频输入 6 元/百万 Token、文本与音频联合输出 12 元/百万 Token；
纯文本输入/输出为 1.5/4.5 元/百万 Token。新加坡音频输入/联合输出为 6.781/13.636。
下文 8 月价格及其单轮估算只保留历史参考，不能用于当前报价；实际费用仍看账单。

[客户端事件文档](https://help.aliyun.com/zh/model-studio/fun-audiochat-client-events)
确认 Audio 3 使用 `longanqian`、16 kHz 输入/24 kHz 输出，并支持 `turn_detection: null`
的手动提交。保留 `max_history_turns: 5`；它是历史上下文限制，不是回答长度限制。
该文档未列出旧 Omni 的 `max_tokens` 或独立 ASR 模型配置，因此不照搬这些字段。
保留简短回答提示、五轮半双工和播放进度驱动字幕，不新增 VAD、联网搜索或工具调用。

### 实机反馈与模型授权

2026-09-28，dev.2 的安全诊断记录为：WebSocket 已连接、会话已创建、设置已发送，
尚未收到设置确认，服务端以 `1007` 关闭连接。这个数值本身不足以断定权限、模型名称
或消息编码问题；设备没有记录服务端关闭原因原文。

核对[官方 WebSocket 接入文档](https://help.aliyun.com/zh/model-studio/qwen-audio-realtime-websocket-api)
后，原有 DashScope 共享域名仍受支持，不需要仅为此次迁移改成 Workspace 专属域名。
[模型使用指南](https://help.aliyun.com/zh/model-studio/qwen-audio-realtime-user-guides)
列有北京和新加坡地域；API Key、模型调用权限与接口需对应同一地域和业务空间。

用户随后确认新模型尚未授权，完成授权后在同一 dev.2 固件上测试对话正常，并同意发布
v0.31.0。没有再修改请求参数或更换固件来恢复服务。教程已补充
[模型授权步骤](cloud-voice.md#准备和配置)：保留旧 Key 不代表自动获得新模型权限。

这次反馈确认了授权后的基本对话恢复，不代表中英文五轮、长时间资源回收、闹钟抢占、
全部地域接口或费用 A/B 已逐项验收。剩余验证继续按 [v0.31.0 计划](v0.31-plan.md)跟踪。

## 2026-08-30 历史评估

以下记录当时官方文档、价格和实现。其“当前”“默认”均指当时版本；默认模型与费用结论
已被上述 9 月迁移决定替代，不作为现行配置说明。

当时结论为保持 Omni 默认、Audio 3 作为对比候选，先完成目标板 A/B；这一历史结论不
代表可以继续使用即将下线的服务，也不因模型宣传的“实时双工”而扩展固件交互。

### 历史评估范围

比较遵循当前产品约束，而不是只比较模型宣传参数：

- ESP32-S3 通过 TLS WebSocket 直连阿里云百炼，不增加自建中转服务；
- 用户主动长按发起有界多轮会话，固件采用手动 PTT 和半双工播放；
- 16 kHz、16-bit、单声道 PCM 输入，24 kHz、16-bit、单声道 PCM 输出；
- 屏幕需要显示用户输入转写和模型回复文字；
- 云端不得自动执行页面导航、设置、删除、OTA 或其他设备操作；
- 费用使用北京地域公开原价估算，不计免费额度、活动优惠和业务空间折扣。

调研分为三部分：先核对模型官方能力，再核对固件真正发送和解析的协议字段，最后在相同
网络、音量和测试语料下进行目标板 A/B。官方支持但固件没有实现的能力，不计入当前功能。

### 当时的方案

AI 对话默认关闭。用户开启并提供 API Key 后，固件默认使用
`qwen3-omni-flash-realtime`，也允许选择 `qwen-audio-3.0-realtime-flash`。两者共用同一
套 WebSocket、PCM 采集、流式播放、取消和错误恢复实现。一次设备会话保持同一
WebSocket 与服务端上下文，最多 5 轮、续问等待最长 30 秒；新一轮只在会话开始后的
5 分钟内准入，已经开始的回答不会被总时限截断。

模型相关的实际差异位于
[`conversation_protocol.c`](../src/conversation/conversation_protocol.c)：

- Qwen3 Omni 使用 `Cherry` 音色，并在同一会话中配置
  `qwen3-asr-flash-realtime` 生成用户输入转写；
- Qwen Audio 3 使用 `longanqian` 音色，不配置额外的输入转写模型，等待 Qwen Audio
  原生返回用户输入转写事件；
- 两者都把 `turn_detection` 设为 `null`，由固件显式提交音频并请求回复。

这不是两条并行的云端调用。Qwen3 的辅助转写模型只是同一 Omni Realtime 会话的配置项；
截至本次调研，官方 Omni Realtime 计费说明没有列出独立的输入转写费用，因此成本估算不
重复叠加独立 ASR 服务单价。

### 当时的模型差异

| 项目 | `qwen3-omni-flash-realtime`（当时默认） | `qwen-audio-3.0-realtime-flash`（当时可选） |
| --- | --- | --- |
| 定位 | 通用实时全模态模型 | 面向实时语音对话的专用模型 |
| 官方输入 | 文本、图片、视频、音频 | 文本、音频 |
| 官方输出 | 文本、音频 | 文本、音频 |
| 用户输入转写 | Omni 会话配置辅助 `qwen3-asr-flash-realtime` | API 原生返回输入转写事件 |
| 轮次检测 | Manual、`server_vad` | PTT、`server_vad`、`smart_turn` |
| 多轮上限 | Realtime 指南列出最多 8 轮 | 默认 20 轮，可调至 50 轮，累计音频最多 300 秒 |
| Function Calling | 不支持 | 支持 |
| 当前固件音色 | `Cherry` | `longanqian` |
| 当前固件交互 | 手动 PTT、最多 5 轮、半双工 | 手动 PTT、最多 5 轮、半双工 |
| 当前固件音频 | 16 kHz PCM 输入、24 kHz PCM 输出 | 16 kHz PCM 输入、24 kHz PCM 输出 |

Qwen3 Omni 的图片、视频能力在这块没有摄像头的设备上没有直接价值。Qwen Audio 3 的
`smart_turn` 能结合声学与语义判断用户是否真的说完，减少“嗯”“啊”或背景声造成的误
打断；其 Function Calling 也为以后安全查询天气等能力提供了可能。但当前固件没有启用
这些能力。当前只在有界的同一 WebSocket 会话中保留上下文；会话关闭后不跨次保留，
也不会因为切换模型就变成自动 VAD 或连续双工助手。

Qwen Audio 官方示例会直接返回
`conversation.item.input_audio_transcription.delta/completed`。当前固件已经解析这组事件，
但仍需在目标板上确认所选地域、账号和模型下的用户字幕完整性；不能只根据接口文档认定
实机显示已经通过。

### 历史费用估算（2026-08-30，已过时）

两种模型的音频均按每秒 12.5 Token 折算。北京地域公开原价如下，单位为元/百万 Token：

| 模型 | 音频输入 | 文本加音频输出 |
| --- | ---: | ---: |
| `qwen3-omni-flash-realtime` | 18.9 | 75.1 |
| `qwen-audio-3.0-realtime-flash` | 30 | 100 |

以 10 秒用户语音和 10 秒模型语音为例，两侧各 125 Token：

- Qwen3 Omni：`125 × 18.9 / 1,000,000 + 125 × 75.1 / 1,000,000`
  ≈ **0.01175 元/轮**；
- Qwen Audio 3：`125 × 30 / 1,000,000 + 125 × 100 / 1,000,000`
  ≈ **0.01625 元/轮**；
- 在这个等时长示例中，Qwen Audio 3 约多 0.0045 元，即约高 38.3%。

这只是便于比较的单轮音频增量估算，不是账单承诺。系统提示、文本输入、多轮历史上下文、
实际回复时长、地域、最低一秒取整、免费额度和优惠都会改变结果。当前一次 WebSocket
会话会累积最多 5 轮上下文；5 轮、30 秒续问和 5 分钟准入边界只限制用户交互，
不代表账单上限。

### 当时拟定的开发板对比测试

在相同 API Key 地域、Wi-Fi、扬声器音量和测试距离下，各模型至少完成 20 段会话，
每段覆盖 3—5 轮：

1. 使用固定语料覆盖中文、英文、中英混说、数字日期、单词解释与发音、短句、接近 10 秒
   的长句、无声和日常背景声；
2. 记录连接成功率、用户字幕完整率、回答正确性、上下文连贯性、首段字幕延迟、
   首段语音延迟、整轮与整段耗时、播放中续问和失败后新会话恢复；
3. 连续成功与失败交错测试，比较内部 RAM、PSRAM、任务栈、音频设备和 Wi-Fi 是否完整
   回收；
4. 在百炼控制台核对实际 Token 与账单，不能只用公式推断成本；
5. 分别记录识别、回答、音色自然度和听感，不把单次主观体验当作最终结论。

上述 A/B 是当时为主动选型设定的验收方案，不代表已经执行。9 月因服务下线而迁移，
仍需验证新默认模型的字幕、首音延迟、语种效果、资源恢复和实际费用。`smart_turn`、
自动 VAD、唤醒词、全双工回声处理和 Function Calling 仍是独立产品功能，
不能与模型默认值变更一起顺带开启。

## 官方资料

- [`qwen3-omni-flash-realtime` 模型信息](https://help.aliyun.com/zh/model-studio/qwen3-omni-flash-realtime)
- [Qwen-Omni Realtime 使用指南](https://help.aliyun.com/zh/model-studio/realtime)
- [`qwen-audio-3.0-realtime-flash` 模型信息](https://help.aliyun.com/zh/model-studio/qwen-audio-3-0-realtime-flash)
- [Qwen-Audio Realtime 使用指南](https://help.aliyun.com/zh/model-studio/qwen-audio-realtime-user-guides)
- [Qwen-Audio Realtime 服务端事件](https://help.aliyun.com/zh/model-studio/qwen-audio-realtime-server-events)
- [Realtime API 协议概述](https://help.aliyun.com/zh/model-studio/realtime-api-overview)
- [阿里云百炼模型价格](https://help.aliyun.com/zh/model-studio/model-pricing)
