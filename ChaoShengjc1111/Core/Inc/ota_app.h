#ifndef __OTA_APP_H
#define __OTA_APP_H

#include "main.h"

/* OTA 升级标志：存放在备份区最后一页(0x08005C00页)的0x08005FF0地址
 * Bootloader 与 APP 约定：0xA5A5 = 待升级，0xFFFF = 正常运行 */
#define OTA_FLAG_ADDR       0x08005FF0u
#define OTA_FLAG_UPGRADE    0xA5A5u
#define OTA_FLAG_NORMAL     0xFFFFu

/* 写入升级标志并软复位，进入Bootloader等待接收固件 */
void OTA_TriggerReboot(void);

#endif
