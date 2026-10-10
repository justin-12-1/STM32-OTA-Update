# 整理清单

## 删除的文件

本次从开源整理副本中删除所有 `Debug/`、`Release/` 目录中的生成文件，以及名称以 `.bak` 结尾和编辑器 `~` 备份文件。原始上传压缩包未修改。

删除文件数：297

## 保留

- STM32CubeIDE 工程文件（`.project`、`.cproject`、`.ioc`、`.settings`）
- C/头文件、启动汇编、链接脚本
- HAL、CMSIS、FreeRTOS 源码及随附许可证
- 外设驱动与 OTA 源码

## 额外新增

- `.gitignore`、`README.md`
- `Docs/flash_partition.md`
- `Docs/ota_protocol.md`
- `Docs/hardware_connection.md`
- `Docs/test_plan.md`
- `Docs/code_review.md`
- `Docs/THIRD_PARTY_NOTICES.md`
- `Docs/cleanup_manifest.md`
