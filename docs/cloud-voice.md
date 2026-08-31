# AI 语音 Beta

本功能自 v0.20.0 正式固件开始提供。AI 语音是“语音”页中的可选 Beta 能力，不增加新页面，也不
替代设备上的 ESP-SR MultiNet 离线指令。默认接入只需提供自己的阿里云百炼 API Key；
无需创建百炼应用，也无需填写 Workspace ID 或 App ID。需要使用新加坡地域或业务空间
专属域名时，才在高级设置中填写官方 API Host。

云端语音未开启、未配置 API Key、家庭 Wi-Fi 当前离线或设备处于 `SAVING` 时，设备会
直接使用本地 MultiNet，不等待云端连接超时。

## 能力边界

- AI 模式用于有界的中文或英文多轮问答。设备流式接收回复，并在屏幕上
  显示文字、通过扬声器播放语音；
- 会话由用户主动长按发起，设备没有唤醒词，也不会在空闲时持续开启麦克风；
- 一次会话在同一条 TLS WebSocket 上保留服务端上下文，最多 5 轮。每轮回复后
  最多等待 30 秒续问，新一轮只在会话开始后的 5 分钟内准入；已经开始的收音和回答
  会完整结束。结束、取消、失败或到达边界后关闭连接并清理临时上下文，下次需要重新
  长按开始；
- 回复文字按实际字体宽度换行并持续显示最新 4 行；新内容覆盖最旧行，不追加省略号。
  状态缓冲满时保留最新的完整 UTF-8 文本；
- 当前仍是手动 PTT 半双工：收音时不播放，播放时不收音。播放中短按 `KEY`
  只会先停止当前回复，然后进入下一轮；这不是全双工语音打断；
- 云端回复只用于显示和播放，不会打开页面、修改设置、清除 Wi-Fi、删除图片、控制闹钟、
  安装固件或执行其他设备动作；设备导航仍由离线 MultiNet 的安全指令完成；
- 固件固定 `wss://`、Realtime 路径和受控模型参数。高级设置只能填写受支持的阿里云官方
  API Host，不能填写完整 URL、路径、端口、IP、代理、自建转发或其他厂商地址；
- 默认模型是 `qwen3-omni-flash-realtime`。高级设置可改为
  `qwen-audio-3.0-realtime-flash`，两者都只使用手动 PTT、有界多轮文字与语音回复，不启用
  `smart_turn`、Function Calling、联网搜索或其他自动操作能力。

两种模型均使用 16 kHz、16-bit、单声道 PCM 输入和 24 kHz、16-bit、单声道 PCM 输出。
模型选择只影响下一次新会话，不能突破上述产品边界。两者的官方能力、固件实际差异、费用
口径和目标板 A/B 方案见[AI 语音模型评估](cloud-voice-evaluation.md)。

## 准备和配置

按下面 7 步即可完成默认北京地域的配置：

1. 登录阿里云账号，打开[百炼 API Key 控制台](https://bailian.console.aliyun.com/?apiKey=1)；
   如果账号尚未开通百炼，先按页面提示开通服务；
2. 确认控制台地域为**华北 2（北京）**，然后点击“创建 API Key”；
3. 按默认按量付费方式创建后，立即复制并妥善保存 Key。这里需要的是百炼模型服务的
   **API Key**，不要填写阿里云 RAM 的 AccessKey ID 或 AccessKey Secret；Token Plan、
   Coding Plan 等专属套餐 Key 不在当前固件的验证范围内。详细步骤可参考
   [阿里云官方说明](https://help.aliyun.com/zh/model-studio/get-api-key)；
4. 在设备“设置”页按住 `KEY` 3 秒，连接屏幕显示的临时热点并打开设置门户；
5. 在“云端语音”中选择开启，将刚才复制的 API Key 直接粘贴到输入框；
6. 保持默认模型，并让 API Host 留空。默认北京共享接口不需要 Workspace ID、App ID，
   也不需要创建百炼应用；
7. 保存后关闭设置门户，等待设备恢复家庭 Wi-Fi。设备处于 `NORMAL` 且取得 IPv4 后，
   下一次语音会话就会使用云端。

API Key 使用普通文本框以兼容手机设置窗口粘贴，输入内容在本次编辑时可见；保存后输入框
立即清空，状态接口也不会回显已保存内容。只修改开关、模型或 API Host 时将 API Key
留空，会保留原 Key；关闭云端语音也只停用云端，不会删除 Key。“清除云端语音配置”的
独立确认操作会删除已保存 Key、关闭云端，并恢复默认模型与默认 API Host。配置保存或
清除后无需重启设备。

### API Host

API Host 留空时使用北京共享域名 `dashscope.aliyuncs.com`，这是只填写 API Key 的默认路径。
阿里云说明原有 DashScope 共享域名当前仍可继续使用，同时建议生产环境迁移到业务空间专属
域名。固件允许以下四种形式：

- `dashscope.aliyuncs.com`：北京共享域名；
- `dashscope-intl.aliyuncs.com`：新加坡共享域名；
- `<workspace-id>.cn-beijing.maas.aliyuncs.com`：北京业务空间专属域名；
- `<workspace-id>.ap-southeast-1.maas.aliyuncs.com`：新加坡业务空间专属域名。

这里只填写主机名，不要加 `wss://`、路径、查询参数或端口。固件内部按受控规则组成完整
Realtime URL，并拒绝其他域名。API Key 必须与 API Host 的地域和业务空间相匹配，固件
不会在鉴权失败后自动跨地域重试。专属域名中的 Workspace ID 由阿里云生成并已经包含在
主机名中，设置门户不再单独索取 Workspace ID。

非默认 API Host 不是秘密，设置页会回显已保存值以便继续编辑；默认北京共享 Host 仍以
空白表示。

## 使用

在“语音”页：

1. 按住 `KEY` 2 秒；
2. 看到提示后松开 `KEY`，再开始说话；
3. 单次输入最长 10 秒，讲话结束后可短按 `KEY` 提前结束；
4. 思考和播放期间等待回复；轮次未满时，播放中短按 `KEY` 可跳过剩余回复并
   在短暂的 `NEXT TURN` 状态后进入下一轮；
5. 回复播放完后显示 `FOLLOW-UP` 和轮次。在 30 秒内短按 `KEY` 续问，
   无需再长按；短按 `BOOT` 结束本次会话；
6. 连接、监听、思考或播放期间短按 `BOOT` 会取消整段会话。第 5 轮完成、
   `FOLLOW-UP` 等待超过 30 秒或 5 分钟准入期限到达后不再开始新一轮；正在进行的
   收音或回答不会被该总时限截断。

闹钟的优先级高于语音。闹钟触发时会取消当前云端或离线会话，清理临时缓冲，再开始提醒；
设置门户、本地或在线 OTA、图库下载和 microSD 写事务也不会与语音会话并行。

## 在线、离线与省电

一次语音会话开始时，固件按当前状态选择后端：

| 条件 | 使用方式 |
| --- | --- |
| 云端语音已开启、已保存 API Key、设备为 `NORMAL`、家庭 Wi-Fi 当前已取得 IPv4 | 阿里云百炼 Realtime |
| 云端语音已关闭、未配置 API Key、家庭 Wi-Fi 离线或设备为 `SAVING` | 本地 ESP-SR MultiNet |
| 云端条件不满足，且本地模型缺失或麦克风不可用 | 显示语音不可用，不影响其他功能 |

后端在整段会话开始时确定，不在后续轮次或讲话过程中切换。鉴权失败、服务端错误或
中途断网时，本次 AI 会话显示失败并安全结束，不把已上传的音频再交给本地识别；
用户重新长按发起新会话时才重新选择后端。离线 MultiNet 仍只识别固件提供的安全导航指令，
不是任意中文听写或聊天模型。

## 数据、凭据与费用

选择 AI 语音后，设备会通过 TLS WebSocket 向阿里云百炼发送各轮 16 kHz 单声道 PCM、
所选模型和协议所需元数据，并接收识别文字、回复文字和 24 kHz 合成语音。服务端在同一
WebSocket 内保留本次会话的上下文；设备不把这些对话历史写入 Flash、NVS、microSD 或
USB 日志。原始 PCM、转写、回复和播放缓冲只在本次会话的易失内存中处理；但语音
内容已经上传给第三方云服务。使用前应自行确认阿里云的服务条款、数据处理规则和账号地域
设置是否适合自己的内容。

API Key 保存在设备 NVS 中。设置门户状态接口不会返回完整 Key，输入框不会预填，固件也
不会主动把 Key、Authorization 请求头、语音 PCM 或对话正文写入日志。正式构建把编译期
最高日志级别锁定为 `INFO`，但这不能替代存储加密：**当前固件未启用 Flash Encryption
或 NVS Encryption**，能够物理读取设备 Flash 的人仍可能提取 API Key 和其他已保存凭据。

建议：

- 为设备或本项目创建专用、可轮换的 Key，不复用管理员 Key 或其他生产业务 Key；
- 在阿里云控制台设置预算、账单提醒或其他消费保护，并定期检查调用记录；
- 设备遗失、转让或送修前先清除云端语音配置，并在控制台吊销旧 Key；
- 不在语音会话中提供密码、验证码、私钥或其他敏感内容。

百炼 Realtime 模型按实际输入和输出 Token 计费，不同模型、地域、免费额度和活动价格可能
变化。多轮时服务端上下文也可能计入用量；5 轮、30 秒续问和 5 分钟准入边界不是固定费用
或账单上限。请以使用时的[阿里云百炼模型价格](https://help.aliyun.com/zh/model-studio/model-pricing)
和控制台账单为准。

## 常见问题

### 已保存 API Key，但仍显示本地语音

确认设置门户中的云端语音已经开启、设备不是 `SAVING`，并检查首屏 Wi-Fi 图标或“状态”
页的 `WI-FI` 行。只保存过网络名称不代表当前在线；家庭网络重新取得 IPv4 后，下一次
新会话才会选择 AI 语音。

### 云端连接或鉴权失败

确认 API Key 仍有效、具有所选模型的调用权限，并与 API Host 的地域和业务空间相符。
使用默认共享域名失败时，不要随意填写第三方地址；如果账号要求专属域名，应从百炼业务
空间详情复制官方主机名。屏幕只显示简短失败原因，USB 日志只记录非敏感的阶段、错误名或
错误码，不会输出 Key、请求头、语音或对话正文。

### 屏幕有回复但没有声音

在设置门户检查播放音量，并确认扬声器诊断正常。音量为零时仍可查看已经返回的文字；
扬声器硬件不可用时，固件不会选择需要语音播放的云端后端。修复硬件或调整音量后再开始
一轮会话。

## 官方资料

- [Realtime API 的 API Key 鉴权与共享 WebSocket 地址](https://help.aliyun.com/en/model-studio/realtime-token-authentication)
- [阿里云百炼 Base URL 与业务空间专属域名](https://help.aliyun.com/zh/model-studio/base-url)
- [`qwen3-omni-flash-realtime` 模型信息](https://help.aliyun.com/zh/model-studio/qwen3-omni-flash-realtime)
- [Qwen-Omni Realtime 客户端事件](https://help.aliyun.com/zh/model-studio/client-events)
- [`qwen-audio-3.0-realtime-flash` 模型信息](https://help.aliyun.com/zh/model-studio/qwen-audio-3-0-realtime-flash)
- [Qwen-Audio Realtime 使用指南](https://help.aliyun.com/zh/model-studio/qwen-audio-realtime-user-guides)
- [阿里云百炼模型价格](https://help.aliyun.com/zh/model-studio/model-pricing)
