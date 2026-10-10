#ifndef __BEEP_H
#define __BEEP_H

/* 蜂鸣器 TIM3_CH1 PWM (PA6)
 * CubeMX已初始化: PSC=71, ARR=399 -> 2.5kHz
 */
void Beep_PWM_Init(void);
void Beep_On(void);
void Beep_Off(void);
void Beep_ShortBeep(void);

#endif
