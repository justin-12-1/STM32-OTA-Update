#ifndef __DELAY_H
#define __DELAY_H

#include "main.h"

/* HAL库工程延时模块
 * delay_ms  -> HAL_Delay()
 * delay_us  -> DWT内核计数器精确延时(不占用SysTick)
 * delay_init-> 使能DWT计数器，接口兼容标准库
 */
void delay_init(uint8_t SYSCLK);
void delay_us(uint32_t nus);
void delay_ms(uint16_t nms);

#endif
