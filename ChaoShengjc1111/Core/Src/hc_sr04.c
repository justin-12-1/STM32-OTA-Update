#include "main.h"
#include "tim.h"
#include "delay.h"
#include "hc_sr04.h"

/* PA1 Trig 单片机输出 -> 发送触发信号 */
/* PA0 Echo 单片机输入 -> 接收回波信号 */
/* TIM4 用于测量回波高电平持续时间 */

volatile int distance = 0;   /* 距离(cm)，中断更新，主循环读取 */
volatile int hc_flag  = 0;   /* 测量完成标志，中断置1 */

/**
  * @brief  HC-SR04初始化
  * @note   Trig/Echo/EXTI/TIM4 全部由CubeMX生成的MX_GPIO_Init()、MX_TIM4_Init()完成，
  *         此处无需再写寄存器配置
  */
void hc_sr04_init(void)
{
    /* CubeMX已初始化所有外设 */
}

/**
  * @brief  发送一次触发信号
  * @note   Trig低电平2ms -> 高电平10us -> 拉低
  */
void hc_sr04_start(void)
{
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_RESET);
    delay_ms(2);

    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_SET);
    delay_us(10);

    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_RESET);
}

/**
  * @brief  获取一次距离(单位cm)
  * @retval >=0 距离值; -1 超时未收到回波(测量失败)
  */
int get_distance(void)
{
    uint32_t timeout = 0;

    hc_flag = 0;
    hc_sr04_start();

    while (hc_flag == 0)
    {
        if (++timeout > 30000)   /* 约数十ms超时，防止卡死 */
        {
            HAL_TIM_Base_Stop(&htim4);   /* 防止TIM残留运行状态 */
            distance = -1;
            break;
        }
    }
    return distance;
}

/**
  * @brief  EXTI0中断回调（EXTI0_IRQHandler由CubeMX生成并自动调用本函数）
  * @note   上升沿: Echo变高 -> TIM4清零并开始计时
  *         下降沿: Echo变低 -> 停止计时，计算距离
  *         计数频率10kHz，每计数1=0.1ms
  *         距离(cm) = 34000cm/s x 时间(s) / 2 = 1.7 x 计数值
  */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == GPIO_PIN_0)
    {
        if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_SET)   /* 上升沿，开始计时 */
        {
            __HAL_TIM_SET_COUNTER(&htim4, 0);
            HAL_TIM_Base_Start(&htim4);
        }
        else                                                       /* 下降沿，停止计时并计算 */
        {
            uint32_t cnt = __HAL_TIM_GET_COUNTER(&htim4);
            HAL_TIM_Base_Stop(&htim4);

            distance = (int)(cnt * 17 / 1000);   /* 单位cm */
            hc_flag = 1;
        }
    }
}
