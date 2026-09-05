# 天气 Beta

天气是可选的日常页面，用于显示所选地点的室外天气和三日预报。它不会替换首屏的
SHTC3 温湿度；首屏继续表示设备附近的室内环境。

设备使用用户自己的 QWeather API Host 和 API Key，通过 HTTPS 直接请求 QWeather，
不经过项目服务器。功能启用后，天气页即加入 `BOOT` 页面环；还没有可用数据时，同一布局
会显示正在获取或失败原因。未配置或未开启天气不会影响 RTC、月历、传感器、图片和离线
语音。

## 准备 QWeather 凭据

先在 QWeather 创建独立的项目和 API Key，并取得账号专属的 API Host：

- API Key 的创建和使用方式见[身份认证](https://dev.qweather.com/docs/configuration/authentication/)；
- API Host 的查看和格式见[API Host](https://dev.qweather.com/docs/configuration/api-host/)；
- 初次使用可从[开发者入门](https://dev.qweather.com/docs/start/)了解项目、凭据和请求的关系。

当前固件只接受 API Key 鉴权。API Host 只填写形如
`abc123.xy.qweatherapi.com` 的主机名，不要添加 `https://`、路径、参数或端口。

建议为设备单独创建可轮换的项目和 Key，只启用天气与地点解析所需服务，不复用管理用途
或其他生产业务的凭据；同时在控制台配置可用的额度、账单提醒或消费保护，并定期检查
用量。

## 配置

1. 在设备“设置”页按住 `KEY` 3 秒，连接屏幕所示的临时热点并打开设置门户；
2. 在“天气”中开启功能，填写 API Host，并粘贴 API Key；
3. 先选择省份，再选择城市；
4. 选择“保存并获取天气”。设置门户会关闭，设备使用已保存的家庭 Wi-Fi 直接连接
   QWeather，并打开天气页一次；设备不会重启；
5. 天气页使用固定布局显示获取进度、结果或失败原因，不会切换到临时全屏状态页，也不会
   在 3 秒后自动返回。以后短按 `BOOT` 可在“首屏 → 天气 → 月历 → 图片（可用时）”
   之间切换，其他页面始终可正常使用。

首版内置中国大陆省、市两级选择目录，因此级联选择不依赖设置热点之外的互联网。设备只
保存当前所选城市和解析结果，不会批量抓取或保存 QWeather GeoAPI 地点库。

保存后设置页不会回显或预填 API Key。以后只修改地点或 API Host 时，将 Key 留空即可
保留原值；清除天气配置需要使用独立确认，并会停用天气页和删除已保存凭据。

## 页面信息

- 顶栏左侧是城市，右侧是 RTC 的当前日期和星期，例如 `9月5日  周六`。日期不依赖天气
  请求，断网或显示旧缓存时仍按 RTC 走时；RTC 无效时显示 `--月--日  周-`；
- 中间保留室外实况和三日预报。“今天、明天、后天”相对 RTC 当前日期计算，旧预报用
  实际月日标注，不会把昨天的缓存当成今天；
- 底部单独显示数据来源与获取时间，例如 `QWeather | 更新 09-05 10:20`。较旧数据标注
  “缓存”或“已过期”，时间未知时直接说明；这个时间取实况和预报中较早的一份，不是当前
  时钟。刷新中和失败时，有效缓存及其获取时间仍保留；
- 最下方是按键提示。长城市名限制在顶栏左侧区域，不覆盖日期；日期变化才触发相应重绘，
  不为日期显示增加每秒刷屏。

## 刷新与离线使用

- 天气服务初始化时执行一次本地 Gzip 解码自检；自检失败会停用天气服务并记录错误，
  其他页面继续启动。刷新后 USB 日志记录天气任务栈和内部堆余量，不包含 API Key；
- 在天气页按住 `KEY` 2 秒可立即刷新。长按 1 秒后会显示剩余时间，提前松开只取消操作；
  正在刷新或已有刷新请求时不会重复排队；
- `NORMAL` 下实时天气约每 30 分钟刷新，三日预报约每 6 小时刷新；地点没有变化时不重复
  解析；
- 每次成功响应都会更新设备中的紧凑缓存。同一地点已有缓存时，网络不可用或后台刷新失败
  仍显示最后一次成功数据，并按数据时间显示过期状态，不用失败响应覆盖缓存；
- `SAVING` 不为天气执行周期联网。用户在天气页手动刷新或在设置门户明确“保存并获取天气”
  时仍可临时联网，完成后按省电规则关闭无线；
- 未插 microSD 不影响天气。缓存保存在设备 Flash 的 NVS 中，不依赖存储卡；
- 更换地点、API Host 或凭据后，新的有效数据会替换旧地点缓存。第一次获取失败时，天气页
  保持稳定布局并显示可操作的失败原因，不显示旧地点数据，也不影响其他本地页面。

QWeather 对实时天气和逐日预报给出了不同的建议缓存周期；本项目的间隔与其建议范围一致。
具体依据见[缓存最佳实践](https://dev.qweather.com/docs/best-practices/cache/)。

## 数据、凭据与费用

设备会把所选城市发送给 QWeather GeoAPI 解析位置，并请求该位置的实时天气和三日
预报。页面显示 `QWeather` 数据来源，相关要求见
[注明来源](https://dev.qweather.com/docs/terms/attribution/)。

API Host、API Key、所选地点和天气缓存保存在设备 NVS。状态接口与设置页面不返回完整
Key，固件也不会主动把 Key 或鉴权请求头写入 USB 日志。**当前固件未启用 Flash
Encryption 或 NVS Encryption**；能够物理读取设备 Flash 的人仍可能提取 Key 和其他
凭据。设备遗失、转让或送修前，应清除天气配置并在 QWeather 控制台吊销旧 Key。

按默认间隔估算，一台持续运行的设备每 30 天约产生 1,560 次天气请求，另有首次或地点
变化时的位置解析以及失败重试。调用计入用户自己的 QWeather 账号，免费额度、阶梯价格、
请求定义和活动政策都可能变化；实际费用与账单以
[QWeather 按量计费定价](https://dev.qweather.com/docs/finance/pricing/)和控制台为准。

## 故障排查

### 天气页显示获取失败

先查看天气布局内显示的失败原因。确认家庭 Wi-Fi 和互联网可用，API Host 只包含
QWeather 主机名，API Key 有效且能访问 GeoAPI、实时天气和逐日预报。修正配置后再次
选择“保存并获取天气”，或在天气页按住 `KEY` 2 秒重试；天气页在请求期间和失败后都
保持可见。

### 地点结果不符合预期

确认省份和城市选择正确。修改城市后重新保存，设备会重新解析并更新缓存。

### 页面显示旧数据

查看页面的数据时间和过期标识。`NORMAL` 会在网络恢复后继续刷新；`SAVING` 不主动联网，
可在天气页按住 `KEY` 2 秒主动获取一次。旧缓存保留是离线降级，不表示当前互联网可用。

## 官方资料

- [构建 API 请求](https://dev.qweather.com/docs/configuration/api-config/)
- [城市搜索](https://dev.qweather.com/docs/api/geoapi/city-lookup/)
- [实时天气](https://dev.qweather.com/docs/api/weather/weather-current/)
- [逐日天气预报](https://dev.qweather.com/docs/api/weather/weather-daily-forecast/)
- [缓存最佳实践](https://dev.qweather.com/docs/best-practices/cache/)
- [按量计费定价](https://dev.qweather.com/docs/finance/pricing/)
- [注明来源](https://dev.qweather.com/docs/terms/attribution/)
