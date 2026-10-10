#include "main.h"
#include "tim.h"
#include "delay.h"
#include "beep.h"

/**
  * @brief  蜂鸣器PWM初始化
  * @note   TIM3_CH1的寄存器配置由CubeMX生成的MX_TIM3_Init()完成，
  *         这里只需启动PWM输出
  */
void Beep_PWM_Init(void)
{
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
}

/** 打开蜂鸣器（占空比=CCR/(ARR+1)=8/400） */
void Beep_On(void)
{
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, 8);
}

/** 关闭蜂鸣器 */
void Beep_Off(void)
{
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, 0);
}

/** 短促响一声 */
void Beep_ShortBeep(void)
{
    Beep_On();
    delay_ms(10);
    Beep_Off();
}
