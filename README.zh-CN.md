# EVtools WIFI-NAG

[English README](README.md)

WIFI-NAG 是面向 Waveshare ESP32-S3 RS485/CAN 板子的固件。本仓库现在只维护一个目标：

- ESP32-S3 原生 TWAI CAN
- CAN ID `880 / 0x370` 的 Nag echo
- WiFi AP + STA
- AP 到 STA 的 NAPT 网关
- DNS 代理 / 过滤 / 缓存
- 中文 WebUI
- 本地手动 OTA

它不是 Legacy/HW3/HW4 FSD 激活固件。MCP2515、SAME51、插件运行时/示例、FSD 激活 handler、CAN recorder/sniffer/debug tools、在线 OTA、设置导入导出、task-stats 页面都不属于当前维护范围。

## 安全提示

本固件仅供开源学习、研究与测试使用。禁止售卖、转售或任何形式的商业化分发。

CAN 写入行为可能影响车辆行为。请先使用 `CAN Write OFF` 验证 RX/状态数据正常；只有在你充分理解功能作用并愿意自行承担风险时，才启用 CAN 写入。驾驶过程中请始终保持清醒、目视前方，并让双手随时准备接管方向盘。

## 硬件

- 板子：Waveshare ESP32-S3 RS485/CAN
- CAN 控制器：ESP32-S3 原生 TWAI
- CAN 收发器：板载 CAN 收发器
- CAN 速率：`500 kbit/s`
- 默认 TWAI TX：`GPIO_NUM_15`
- 默认 TWAI RX：`GPIO_NUM_16`
- 默认热点：`Albert-WX`
- 默认热点密码：`12345678`
- WebUI 地址：`http://100.100.1.1/`

外部接线请使用板子端子上的标识：

```text
CANH -> 车辆 CAN-H
CANL -> 车辆 CAN-L
```

不要把车辆 CAN 线直接接到 ESP32 GPIO。

## 当前 CAN 行为

当前唯一启用的 CAN 业务逻辑是 `0x370 / 880` 的 Nag echo。

### 通用规则

- 只监听 CAN ID `880 / 0x370`。
- 忽略 DLC 小于 8 的帧。
- `CAN Write OFF`：只读监听，不发送 Nag echo。
- `CAN Write ON`：允许当前 Nag 模式按各自门控规则发送 echo。
- 输出 echo 帧写入 `EPAS3S_handsOnLevel = 1`。
- 更新 `data[6]` 低 4 bit counter。
- 重新计算 checksum byte `data[7]`。
- 跳过自己刚发出的 echo，避免反馈循环。

### 模式 A

- 固定输出扭矩：`+1.80 Nm`。
- `CAN Write ON` 时，对每个真实 `0x370` 帧发送。

### 模式 A_V2

- 在配置范围内做伪随机扫动。
- 扫动周期：`2000 ms`。
- 默认范围：`+1.50 .. +1.80 Nm`。
- 范围会限制在 `-1.80 .. +1.80 Nm`。
- 如果最小值大于最大值，会自动交换。

### 模式 ADAPTIVE（V4.3-V13）

- 闭环同时观察校验有效的真实 EPAS `0x370` 和 DAS `0x39B`；`0x39B` 提供 HOS 状态，并受可配置 freshness 窗口约束。
- 未见 DAS 或最后一帧超过 freshness 窗口时进入 `WAIT_DAS`，保持 no-send；即使总线之后完全静默，Web/API 也会按最后时间戳把诊断更新为 stale。
- DAS 恢复后进入 `ARMING`，需要连续 3 个有效 OEM EPAS 帧才允许输出。
- HOS 0..2 属于正常范围，HOS 2 是当前系统常态；`MAINTENANCE` 对每个有效原车 `0x370` 都注入随机 `1.50 .. 1.80 Nm`，连续约 10 秒。
- 10 秒非零注入后进入 `REST` 停发间隔，连续 1～2 秒不额外发送 `0x370`，随后自动开始下一轮非零注入；不会注入 `0 Nm`。
- HOS 3..5 进入 `CORRECTIVE`：先清除上一目标，再对每个有效原车帧连续注入随机 `1.80 .. 2.00 Nm`，直到 DAS 回到 HOS 0..2；不再使用短 burst、`VERIFY` 或两次尝试上限。
- HOS 6..15 均 fail-closed 并进入 `FAULT_HOLD` 保护停发，其中 9..14 为未定义状态；DAS stale 同样阻止发送。HOS 0..2 连续稳定 2000 ms 后自动恢复，显式 reset 立即 off/on。
- 所有动态扭矩均限制在 `±2.00 Nm`，counter、checksum、own-echo 跳过及 stale/fault no-send 约束保持不变；首次刷入默认使用自适应模式。
- 自适应参数保存在 NVS，重启后继续生效；详细状态机与参数见 [`docs/nag-adaptive-closed-loop.md`](docs/nag-adaptive-closed-loop.md)。
- Gate B/C 实车验证仍为 **PENDING**，本文不声称已经完成实车验证。

## WiFi / DNS 网关

- 启动 AP + STA 模式。
- 默认 AP 网段：`100.100.1.x`。
- 设备/网关 IP：`100.100.1.1`。
- 可保存多个上游 WiFi。
- 支持 STA 静态 IP、网关、掩码和 DNS。
- STA 连接后启用 AP 到 STA 的 NAPT。
- 为 AP 客户端运行 UDP 53 DNS 代理。
- 支持黑名单和白名单规则。
- 缓存 DNS 响应，并合并重复 pending DNS 查询。

## WebUI

WebUI 提供：

- CAN 状态、RX/TX/errors、FPS、运行时间
- CAN Write 开关
- Nag 模式、A_V2 范围和 ADAPTIVE HOS 闭环参数
- AP 热点设置
- WiFi 扫描/连接/删除
- STA-AP 网关控制
- DNS 上游、黑名单、白名单、诊断和被拦截域名列表
- 系统状态
- 调试日志查看
- 手动固件上传 OTA
- 网页 OTA 和 ArduinoOTA 均无需账号或密码
- 每次页面加载时显示安全提示弹窗

## 编译

先创建本地凭据文件：

```powershell
Copy-Item platformio_profile.example.h platformio_profile.h
```

如需自定义 AP 名称、AP 密码或 OTA 密码，请编辑 `platformio_profile.h`。该文件已被 git 忽略。

```powershell
pio run -e wifi_nag_ESP32_S3_CAN
```

固件输出：

```text
.pio/build/wifi_nag_ESP32_S3_CAN/firmware.bin
```

## 下载

以 `COM15` 为例：

```powershell
pio run -e wifi_nag_ESP32_S3_CAN -t upload --upload-port COM15
```

## 16MB 完整镜像

可以用下面命令生成合并后的 16MB 完整镜像：

```powershell
py -3 $env:USERPROFILE\.platformio\packages\tool-esptoolpy\esptool.py --chip esp32s3 merge_bin -o dist\wifi_nag_ESP32_S3_CAN_16MB_full.bin --flash_mode dio --flash_freq 80m --flash_size 16MB --fill-flash-size 16MB 0x0 .pio\build\wifi_nag_ESP32_S3_CAN\bootloader.bin 0x8000 .pio\build\wifi_nag_ESP32_S3_CAN\partitions.bin 0x19000 .pio\build\wifi_nag_ESP32_S3_CAN\ota_data_initial.bin 0x20000 .pio\build\wifi_nag_ESP32_S3_CAN\firmware.bin
```

完整镜像会写入整片 Flash，并覆盖 NVS/SPIFFS 设置。

## 测试

```powershell
pio test -e native_nag
pio test -e native_nag_adaptive
pio test -e native_twai
pio test -e native_log_buffer
py -3 -m unittest test/test_wifi_settings_regression.py
```

## WebUI 生成

编辑：

```text
include/web/mcp2515_dashboard_ui.src.h
```

然后重新生成：

```powershell
py -3 scripts/minify_dashboard.py
```

不要手动编辑 `include/web/mcp2515_dashboard_ui.h`，除非是通过上述生成流程。

## 当前维护文件组

```text
include/                         固件核心头文件
include/drivers/                 CAN 驱动抽象和 TWAI 驱动
include/web/                     WebUI、dashboard、DNS 网关
src/                             ESP-IDF 入口和运行时兼容层
scripts/minify_dashboard.py      WebUI 生成器
scripts/platformio_*.py          PlatformIO 辅助脚本
test/                            Native 回归测试
platformio.ini                   构建环境
sdkconfig*.defaults              ESP-IDF 默认配置
partitions_16mb_ota_4096k_nvs64_boot.csv
```

## 不在维护范围

干净版 WIFI-NAG 项目有意排除：

- Legacy/HW3/HW4 FSD 激活
- MCP2515 外置 CAN
- SAME51
- CAN recorder/sniffer/debug tools
- 插件文档/示例/运行时
- 在线 OTA
- 设置导入/导出
- task-stats 页面
- 旧文档站点和 release CI 元数据
