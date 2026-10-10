#include "main.h"
#include "delay.h"

/**
  * @brief  延时初始化（接口兼容标准库）
  * @param  SYSCLK: 系统时钟频率MHz，本例72，仅作兼容保留
  * @note   HAL库的HAL_Delay已使用SysTick，因此delay_us改用DWT内核计数器实现，
  *         两者互不干扰
  */
void delay_init(uint8_t SYSCLK)
{
    /* 使能DWT周期计数器 */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

/**
  * @brief  微秒级延时
  * @param  nus: 延时微秒数
  * @note   72MHz时每微秒72个周期
  */
void delay_us(uint32_t nus)
{
    uint32_t start, ticks;

    if (nus == 0) return;

    ticks = nus * (SystemCoreClock / 1000000U);   /* 换算成周期数 */
    DWT->CYCCNT = 0;
    start = DWT->CYCCNT;
    while ((DWT->CYCCNT - start) < ticks);
}

/**
  * @brief  毫秒级延时
  * @param  nms: 延时毫秒数
  */
void delay_ms(uint16_t nms)
{
    HAL_Delay(nms);
}
