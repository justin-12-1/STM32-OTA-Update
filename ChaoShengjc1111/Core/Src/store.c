#include "main.h"
#include "MyFLASH.h"

/* 数据存储区：Flash末页（地址0x0800FC00，F103C8共64KB） */
#define STORE_START_ADDRESS     0x0800FC00
#define STORE_COUNT             512

uint32_t Store_Data[STORE_COUNT];

/** 初始化：无魔数则先格式化，再读出全部数据到RAM */
void Store_Init(void)
{
	if (MyFLASH_ReadHalfWord(STORE_START_ADDRESS) != 0xA5A5)
	{
		MyFLASH_ErasePage(STORE_START_ADDRESS);
		MyFLASH_ProgramHalfWord(STORE_START_ADDRESS, 0xA5A5);
		for (uint16_t i = 1; i < STORE_COUNT; i++)
		{
			MyFLASH_ProgramHalfWord(STORE_START_ADDRESS + i * 2, 0x0000);
		}
	}

	for (uint16_t i = 0; i < STORE_COUNT; i++)
	{
		Store_Data[i] = MyFLASH_ReadHalfWord(STORE_START_ADDRESS + i * 2);
	}
}

/** 保存：先擦页再全量写入 */
void Store_Save(void)
{
	MyFLASH_ErasePage(STORE_START_ADDRESS);
	for (uint16_t i = 0; i < STORE_COUNT; i++)
	{
		MyFLASH_ProgramHalfWord(STORE_START_ADDRESS + i * 2, Store_Data[i]);
	}
}

/** 清空存储数据 */
void Store_Clear(void)
{
	for (uint16_t i = 1; i < STORE_COUNT; i++)
	{
		Store_Data[i] = 0x0000;
	}
	Store_Save();
}
