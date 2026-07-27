# OTA 分区与回滚（3.0.0-beta.6）

WIFI-NAG 使用 `partitions_16mb_ota_4096k_nvs64_boot.csv`：

```text
boot / ota_0  0x20000  4 MiB
app1 / ota_1  0x420000 4 MiB
```

上传开始时，运行时会：

1. 获取当前实际运行分区；
2. 若 `otadata` 的启动分区不是当前运行分区，先校正到当前运行分区；
3. 以当前运行分区显式选择非活动 OTA 槽；
4. 校验目标槽类型、OTA subtype 与容量；
5. 缓存并校验 ESP32-S3 镜像头和应用描述符后才开始写入；
6. 写入完成后校验分区描述符；
7. 设置新启动槽后重新读取确认；失败时恢复当前运行槽。

构建配置同时启用 bootloader application rollback。新镜像启动后观察 15 秒；仅当 TWAI
驱动处于 `RUNNING` 且未触发安全锁定时，才标记镜像有效。健康检查失败会请求 rollback。

分区表或 bootloader 配置变更不能通过 OTA 部署。首次采用此布局、修复单槽布局或更新
rollback bootloader 时，必须将完整 Factory 镜像从 `0x0` 线刷。
