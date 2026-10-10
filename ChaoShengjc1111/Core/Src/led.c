#include "main.h"
#include "delay.h"
#include "led.h"

/**
  * @brief  LED初始化（CubeMX已配置GPIO，此处仅设初始电平）
  */
void Led_init(void)
{
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4, GPIO_PIN_RESET);
}

/** 点亮报警灯PA2 */
void Led_On(void)
{
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_SET);
    delay_us(5);
}

/** 兼容旧接口 */
void Led_ShortOn(void)
{
    Led_On();
}

/** 熄灭报警灯PA2，点亮PA3/PA4 */
void Led_Off(void)
{
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_RESET);
    delay_us(5);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_3 | GPIO_PIN_4, GPIO_PIN_SET);
    delay_us(5);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
    delay_us(5);
}
