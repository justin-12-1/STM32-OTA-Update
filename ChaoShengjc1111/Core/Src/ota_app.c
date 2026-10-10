#include "ota_app.h"
#include "myflash.h"

/**
  * @brief  写入升级标志并软复位
  * @note   调用前需保证HTTP响应已发出（调用方负责延时）
  */
void OTA_TriggerReboot(void)
{
    /* 擦标志页(0x08005C00~0x08005FFF)再写升级标志，注意该页不属于备份区固件范围 */
    MyFLASH_ErasePage(0x08005C00u);
    MyFLASH_ProgramHalfWord(OTA_FLAG_ADDR, OTA_FLAG_UPGRADE);

    /* 给ESP留时间把"OTA START"响应完整发出去（SEND OK 后约0.5s） */
    HAL_Delay(500);

    /* 软件复位 -> 进入 Bootloader */
    HAL_NVIC_SystemReset();

    while (1) { }   /* 不会执行到这里 */
}
