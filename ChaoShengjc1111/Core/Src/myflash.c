#include "main.h"
#include "myflash.h"

/** 读32位字 */
uint32_t MyFLASH_ReadWord(uint32_t Address)
{
	return *((__IO uint32_t *)(Address));
}

/** 读16位半字 */
uint16_t MyFLASH_ReadHalfWord(uint32_t Address)
{
	return *((__IO uint16_t *)(Address));
}

/** 读8位字节 */
uint8_t MyFLASH_ReadByte(uint32_t Address)
{
	return *((__IO uint8_t *)(Address));
}

/** 擦除全部页（F103C8: 64KB，共64页，每页1KB） */
void MyFLASH_EraseAllPages(void)
{
	FLASH_EraseInitTypeDef EraseInitStruct = {0};
	uint32_t PageError = 0;

	HAL_FLASH_Unlock();
	/* F1系列HAL仅支持按页擦除，循环擦除全部64页 */
	EraseInitStruct.TypeErase = FLASH_TYPEERASE_PAGES;
	EraseInitStruct.Banks = FLASH_BANK_1;
	EraseInitStruct.PageAddress = 0x08000000;
	EraseInitStruct.NbPages = 64;
	HAL_FLASHEx_Erase(&EraseInitStruct, &PageError);
	HAL_FLASH_Lock();
}

/** 擦除指定页 */
void MyFLASH_ErasePage(uint32_t PageAddress)
{
	FLASH_EraseInitTypeDef EraseInitStruct = {0};
	uint32_t PageError = 0;

	HAL_FLASH_Unlock();
	EraseInitStruct.TypeErase = FLASH_TYPEERASE_PAGES;
	EraseInitStruct.Banks = FLASH_BANK_1;
	EraseInitStruct.PageAddress = PageAddress;
	EraseInitStruct.NbPages = 1;
	HAL_FLASHEx_Erase(&EraseInitStruct, &PageError);
	HAL_FLASH_Lock();
}

/** 编程32位字 */
void MyFLASH_ProgramWord(uint32_t Address, uint32_t Data)
{
	HAL_FLASH_Unlock();
	HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, Address, Data);
	HAL_FLASH_Lock();
}

/** 编程16位半字 */
void MyFLASH_ProgramHalfWord(uint32_t Address, uint32_t Data)
{
	HAL_FLASH_Unlock();
	HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, Address, Data);
	HAL_FLASH_Lock();
}
