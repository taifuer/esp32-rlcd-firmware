# AI 语音模型评估

本文记录 ESP32 RLCD Firmware AI 语音模型的选型依据。结论基于 2026-08-30 的阿里云
百炼官方文档、公开原价和当前固件实现；上游能力、价格或协议变化后需要重新核对。

当前结论：保持 `qwen3-omni-flash-realtime` 为默认模型，
`qwen-audio-3.0-realtime-flash` 作为高级设置中的实机对比候选。完成目标板 A/B 验收前，
不因为上游模型标注“实时双工”就更改固件默认值或交互方式。

## 评估范围

比较遵循当前产品约束，而不是只比较模型宣传参数：

- ESP32-S3 通过 TLS WebSocket 直连阿里云百炼，不增加自建中转服务；
- 用户主动长按发起有界多轮会话，固件采用手动 PTT 和半双工播放；
- 16 kHz、16-bit、单声道 PCM 输入，24 kHz、16-bit、单声道 PCM 输出；
- 屏幕需要显示用户输入转写和模型回复文字；
- 云端不得自动执行页面导航、设置、删除、OTA 或其他设备操作；
- 费用使用北京地域公开原价估算，不计免费额度、活动优惠和业务空间折扣。

调研分为三部分：先核对模型官方能力，再核对固件真正发送和解析的协议字段，最后在相同
网络、音量和测试语料下进行目标板 A/B。官方支持但固件没有实现的能力，不计入当前功能。

## 当前实现基线

云端 AI 语音默认关闭。用户开启并提供 API Key 后，固件默认使用
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

## 模型差异

| 项目 | `qwen3-omni-flash-realtime`（当前默认） | `qwen-audio-3.0-realtime-flash`（可选） |
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

## 成本口径

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

## 目标板 A/B 方案

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

只有当 Qwen Audio 3 在用户字幕、首音延迟、中文和英文实际效果上稳定优于当前默认，且
资源恢复与实际费用可接受时，才考虑把它改为默认模型。当前只实现了按键驱动的
有界多轮上下文；`smart_turn`、自动 VAD、唤醒词、全双工回声处理和 Function Calling
仍是独立产品功能，不能与模型默认值变更一起顺带开启。

## 官方资料

- [`qwen3-omni-flash-realtime` 模型信息](https://help.aliyun.com/zh/model-studio/qwen3-omni-flash-realtime)
- [Qwen-Omni Realtime 使用指南](https://help.aliyun.com/zh/model-studio/realtime)
- [`qwen-audio-3.0-realtime-flash` 模型信息](https://help.aliyun.com/zh/model-studio/qwen-audio-3-0-realtime-flash)
- [Qwen-Audio Realtime 使用指南](https://help.aliyun.com/zh/model-studio/qwen-audio-realtime-user-guides)
- [Qwen-Audio Realtime 服务端事件](https://help.aliyun.com/zh/model-studio/qwen-audio-realtime-server-events)
- [Realtime API 协议概述](https://help.aliyun.com/zh/model-studio/realtime-api-overview)
- [阿里云百炼模型价格](https://help.aliyun.com/zh/model-studio/model-pricing)
