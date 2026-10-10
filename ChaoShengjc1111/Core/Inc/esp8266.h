#ifndef __ESP8266_H
#define __ESP8266_H

#include "main.h"

/* ESP8266 WiFi模块 通过 USART1 通信 (PA9 TX, PA10 RX, 115200)
 * CubeMX已配置USART1并开启中断
 */
void ESP8266_USART1_Init(void);
void ESP8266_SendString(char *str);
void ESP8266_AutoConfigAT(void);
uint8_t ESP8266_ProcessESPData(uint16_t cur_dist, uint16_t set_dist, uint16_t *out_set_dist);

#endif
