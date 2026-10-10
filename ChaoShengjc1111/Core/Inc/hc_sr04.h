#ifndef __HC_SR04_H
#define __HC_SR04_H

/* HC-SR04超声波
 * PA1 Trig 输出 (CubeMX已配置)
 * PA0 Echo 输入 + EXTI0双边沿中断 (CubeMX已配置)
 * TIM4 计时: PSC=71 -> 计数频率10kHz, 每计数1=0.1ms (CubeMX已配置)
 */
void hc_sr04_init(void);
void hc_sr04_start(void);
int  get_distance(void);

#endif
