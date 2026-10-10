# 硬件连接与网络配置清单

## 硬件信息（请按实物填写）

- MCU/开发板：`待填写完整型号`
- ESP8266 模块与 AT 固件版本：`待填写`
- USB-TTL 型号与逻辑电平：`待填写（确认 3.3 V UART 逻辑）`
- ST-Link/调试器：`待填写`
- 其他外设及供电：`待填写`

## 串口与引脚

源码注释显示 Bootloader 使用 USART1 作为 USB-TTL 路径、USART3（PB10/PB11）连接 ESP8266，并将 PA7 用作 ESP RST 控制。请对照实际 `.ioc` 和原理图确认，不要仅依注释接线。

| 信号 | MCU 端 | 外设端 | 已核对 |
|---|---|---|---|
| USART1 TX/RX | `待核对` | USB-TTL RX/TX | [ ] |
| USART3 TX/RX | PB10/PB11（源码注释） | ESP8266 RX/TX | [ ] |
| ESP RST | PA7（Bootloader 源码） | ESP8266 RST | [ ] |
| GND | `待填写` | 共地 | [ ] |

## Wi-Fi 与 TCP 配置

1. 在 `ChaoShengjc1111/Core/Src/esp8266.c` 设置 `OTA_WIFI_SSID` 和 `OTA_WIFI_PASSWORD`。
2. 根据实际网络修改 `OTA_ESP_STATIC_IP`、`OTA_GATEWAY_IP` 和 `OTA_NETMASK`。
3. 在 `Bootloader/Core/Src/main.c` 中同步检查 `ESP_STATIC_IP`、`ESP_GATEWAY`、`ESP_PC_IP`、`ESP_PC_PORT` 等配置。Bootloader 源码会主动连接运行接收脚本的电脑。
4. 确认 APP 与 Bootloader 的网络参数一致，电脑和 ESP 能互相连通，并检查防火墙规则。
5. 不要将真实密码提交到 Git；如果此前提交过真实密码，应更换密码。

| 参数 | 当前开源默认值 | 需操作 |
|---|---|---|
| SSID | `YOUR_WIFI_SSID` | [ ] 替换 |
| Wi-Fi 密码 | `YOUR_WIFI_PASSWORD` | [ ] 本地填写，勿提交 |
| ESP 静态 IP | `192.168.1.88` 示例 | [ ] 按网络修改 |
| 网关 | `192.168.1.1` 示例 | [ ] 按网络修改 |
| PC 接收端 IP | `192.168.1.100` 示例 | [ ] 按网络修改 |
| TCP 端口 | `9000` | [ ] 与上位机一致 |

## 供电与风险

- [ ] ESP8266 电源满足模块峰值电流需求。
- [ ] UART 电平兼容，TX/RX 交叉连接，共地。
- [ ] 首次 OTA 在可恢复开发板上进行，准备 ST-Link 重新烧录手段。
