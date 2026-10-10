/* =====================================================================
 * STM32F103C8T6 OTA Bootloader v3（分块 ACK 流控版 + WiFi 远程入口）
 * 用本文件【整体覆盖】Bootloader 工程的 main.c
 *
 * v3 相对 v2 新增:
 *   1) 升级时优先尝试走 WiFi（ESP8266 透传进 USART3）：
 *      启动 -> 试配 ESP 透传连电脑 TCP:9000 -> 成功则从 USART3 收固件
 *      失败（无 ESP/连不上）-> 自动回落 USB-TTL(USART1)，行为同 v2
 *   2) 帧协议 / ACK 流控 / CRC32 / 防砖标志 完全不变
 *
 * 协议:
 *   帧头: AA 55 | bin长度(4LE) | CRC32(4LE)
 *   Bootloader -> "BOOT> GO"
 *   脚本发 1KB 数据块 -> Bootloader 擦页/写 -> "BOOT> ACK"
 *   循环直到发完 -> "BOOT> OTA OK, jump" 或 "BOOT> CRC FAIL"
 *
 * 注意:
 *   - WiFi 透传模式下，ESP 作为 TCP Client 主动连电脑 <PC_IP>:9000
 *   - 静态 IP / 热点账号 / 电脑 IP 换环境需同步改下方宏
 * ===================================================================== */
/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Bootloader v3 (裸机) - 分块 ACK 流控 OTA + WiFi 透传入口
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* USER CODE BEGIN Includes */
#include <string.h>
#include <stdio.h>
/* USER CODE END Includes */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define APP_ADDR         0x08006000u   /* APP 运行区起始 */
#define APP_MAX_SIZE     0x9800u       /* APP bin 最大 38KB */
#define FLAG_PAGE_ADDR   0x08005C00u   /* 升级标志页 */
#define FLAG_ADDR        0x08005FF0u   /* 升级标志地址 */
#define FLAG_UPGRADE     0xA5A5u       /* APP 请求升级 */
#define FLAG_INPROGRESS  0x5A5Au       /* 升级中（断电恢复用） */
#define FLAG_NORMAL      0xFFFFu
#define CHUNK_SIZE       0x400u        /* 每块 1KB */

#define PKT_HEAD1        0xAAu
#define PKT_HEAD2        0x55u

/* ---- ESP8266 透传参数（换环境要改这里） ---- */
/* 开源版本不包含真实网络凭据。按 Docs/hardware_connection.md 配置后再使用。 */
#define ESP_SSID         "YOUR_WIFI_SSID"       /* 用户自行填写：热点 SSID */
#define ESP_PASS         "YOUR_WIFI_PASSWORD"   /* 用户自行填写：热点密码 */
#define ESP_STATIC_IP    "192.168.1.88"         /* 示例地址：需与实际网络规划一致 */
#define ESP_GATEWAY      "192.168.1.1"          /* 示例网关：需按实际网络修改 */
#define ESP_PC_IP        "192.168.1.100"        /* 运行 OTA 接收脚本的电脑 IP，需修改 */
#define ESP_PC_PORT      "9000"

/* ---- ESP 硬件复位引脚（方案2：GPIO 拉低复位，代替 AT+RST 软件复位） ---- */
#define ESP_RST_GPIO_PORT    GPIOA
#define ESP_RST_GPIO_PIN     GPIO_PIN_7   /* ESP RST 引脚接到 PA7 */
/* USER CODE END PD */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
static uint8_t page_buf[CHUNK_SIZE];   /* 块收发缓冲 1KB */
UART_HandleTypeDef huart1;
UART_HandleTypeDef huart3;
static UART_HandleTypeDef *g_ota = &huart1;   /* 当前升级通道（默认 USB） */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void MX_USART1_UART_Init(void);
void MX_USART3_UART_Init(void);
/* USER CODE BEGIN PFP */
static uint32_t Boot_CRC32_Update(uint32_t crc, uint8_t byte);
static void     Boot_Print(const char *s);
static void     Boot_Reply(const char *s);
static uint16_t Boot_ReadFlag(void);
static void     Boot_WriteFlag(uint16_t flag);
static void     Boot_ErasePage(uint32_t addr);
static void     Boot_ProgramBuf(uint32_t addr, const uint8_t *buf, uint32_t len);
static void     Boot_JumpToApp(void);
static uint8_t  ESP_WaitKey(const char *key, uint32_t timeout_ms);
static uint8_t  ESP_WaitKey2(const char *key1, const char *key2, uint32_t timeout_ms);
static uint8_t  ESP_SendAT(const char *cmd, const char *ok_key, uint32_t timeout_ms);
static uint8_t  ESP_TryWiFi(void);
/* USER CODE END PFP */

/**
  * @brief  The application entry point.
  */
int main(void)
{
  /* USER CODE BEGIN 1 */
  uint16_t flag;
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/
  HAL_Init();
  SystemClock_Config();
  MX_USART1_UART_Init();
  MX_USART3_UART_Init();

  /* USER CODE BEGIN 2 */
  Boot_Print("BOOT> ready\r\n");

  flag = Boot_ReadFlag();
  if (flag != FLAG_UPGRADE && flag != FLAG_INPROGRESS)
  {
    Boot_JumpToApp();                 /* 无升级任务，直接跳 APP */
  }

  /* ===== 有升级任务：优先尝试 WiFi 通道，失败回落 USB ===== */
  if (ESP_TryWiFi())
  {
    g_ota = &huart3;
    Boot_Print("BOOT> WIFI LINK OK\r\n");
  }
  else
  {
    g_ota = &huart1;
    Boot_Print("BOOT> USB MODE\r\n");
  }

  /* ===== 外层接收循环：失败后可重新接收新帧（INPROGRESS 时 APP 可能已坏，不能跳） ===== */
  while (1)
  {
    uint8_t  b, hdr[8];
    uint32_t bin_len, bin_crc, cal_crc;
    uint32_t off, remain;

    /* --- 等帧头 AA 55 --- */
    b = 0;
    while (b != PKT_HEAD1)
    {
      /* UPGRADE 时等 30 秒（WiFi 脚本可能稍慢），INPROGRESS 时 3 秒循环死等 */
      if (HAL_UART_Receive(g_ota, &b, 1, (flag == FLAG_UPGRADE) ? 30000 : 3000) != HAL_OK)
      {
        /* 长时间无数据：UPGRADE（APP完好）-> 放弃跳APP；INPROGRESS -> 死等 */
        if (flag == FLAG_UPGRADE)
        {
          Boot_WriteFlag(FLAG_NORMAL);
          Boot_JumpToApp();
        }
        b = 0;
      }
    }
    if (HAL_UART_Receive(g_ota, &b, 1, 2000) != HAL_OK || b != PKT_HEAD2)
      continue;

    if (HAL_UART_Receive(g_ota, hdr, 8, 2000) != HAL_OK)
      continue;

    bin_len = (uint32_t)hdr[0] | ((uint32_t)hdr[1] << 8) |
              ((uint32_t)hdr[2] << 16) | ((uint32_t)hdr[3] << 24);
    bin_crc = (uint32_t)hdr[4] | ((uint32_t)hdr[5] << 8) |
              ((uint32_t)hdr[6] << 16) | ((uint32_t)hdr[7] << 24);

    if (bin_len == 0 || bin_len > APP_MAX_SIZE)
    {
      Boot_Reply("BOOT> LEN ERR\r\n");
      if (flag == FLAG_UPGRADE)       /* 首次触发且APP完好：放弃升级 */
      {
        Boot_WriteFlag(FLAG_NORMAL);
        Boot_JumpToApp();
      }
      continue;                      /* INPROGRESS：等重传 */
    }

    /* --- 标记"升级中"（断电重上电会留在 Bootloader） --- */
    Boot_WriteFlag(FLAG_INPROGRESS);
    flag = FLAG_INPROGRESS;
    Boot_Reply("BOOT> GO\r\n");

    /* --- 分块接收：收满一块 -> 擦页/写 Flash -> 回 ACK --- */
    cal_crc = 0xFFFFFFFFu;
    off     = 0;
    remain  = bin_len;
    {
      uint32_t rx_ok = 1;
      while (remain > 0)
      {
        uint32_t chunk = (remain > CHUNK_SIZE) ? CHUNK_SIZE : remain;

        if (HAL_UART_Receive(g_ota, page_buf, chunk, 10000) != HAL_OK)
        {
          Boot_Reply("BOOT> RX TO\r\n");
          rx_ok = 0;
          break;
        }

        Boot_ErasePage(APP_ADDR + off);
        Boot_ProgramBuf(APP_ADDR + off, page_buf, chunk);

        {
          uint32_t i;
          for (i = 0; i < chunk; i++)
            cal_crc = Boot_CRC32_Update(cal_crc, page_buf[i]);
        }

        off    += chunk;
        remain -= chunk;
        Boot_Reply("BOOT> ACK\r\n");
      }

      if (!rx_ok) continue;          /* 回到等帧状态，重跑脚本即可 */
    }

    cal_crc ^= 0xFFFFFFFFu;

    if (cal_crc != bin_crc)
    {
      Boot_Reply("BOOT> CRC FAIL\r\n");
      continue;                      /* 保持 INPROGRESS，等重传 */
    }

    /* --- 升级成功：清标志，跳转新 APP --- */
    Boot_WriteFlag(FLAG_NORMAL);
    Boot_Reply("BOOT> OTA OK, jump\r\n");
    /* 退出 ESP 透传，恢复 AT 模式（否则新 APP 的 AT 指令会被透传吞掉，配不上 ESP） */
    if (g_ota == &huart3)
    {
      HAL_Delay(200);                                  /* +++ 前留静默间隔 */
      HAL_UART_Transmit(&huart3, (uint8_t *)"+++", 3, 300);
      HAL_Delay(1000);                                 /* 等 ESP 退出透传 */
    }
    HAL_Delay(50);
    Boot_JumpToApp();
  }
  /* USER CODE END 2 */

  /* Infinite loop */
  while (1) { }
}

/**
  * @brief System Clock Configuration (HSE 8MHz * 9 = 72MHz)
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                              | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/** USART1 初始化：115200/8N1（GPIO 由 HAL_UART_MspInit 完成） */
void MX_USART1_UART_Init(void)
{
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
}

/** USART3 初始化：115200/8N1（ESP8266，PB10/PB11） */
void MX_USART3_UART_Init(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_USART3_CLK_ENABLE();

  /* PB10 = USART3_TX（复用推挽） */
  gpio.Pin = GPIO_PIN_10;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &gpio);

  /* PB11 = USART3_RX（输入浮空） */
  gpio.Pin = GPIO_PIN_11;
  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &gpio);

  huart3.Instance = USART3;
  huart3.Init.BaudRate = 115200;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
}

/** CRC32 增量（IEEE 802.3, 初始0xFFFFFFFF, 结束取反；与 zlib.crc32 一致） */
static uint32_t Boot_CRC32_Update(uint32_t crc, uint8_t byte)
{
  int j;
  crc ^= byte;
  for (j = 0; j < 8; j++)
    crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1));
  return crc;
}

/** 串口打印一行（调试用：走 USB 口） */
static void Boot_Print(const char *s)
{
  HAL_UART_Transmit(&huart1, (uint8_t *)s, (uint16_t)strlen(s), 100);
}

/** 协议回复（GO/ACK/OK 等）：走当前升级通道 g_ota
 *  USB 模式 g_ota=huart1，WiFi 模式 g_ota=huart3（经 ESP 透传回电脑） */
static void Boot_Reply(const char *s)
{
  HAL_UART_Transmit(g_ota, (uint8_t *)s, (uint16_t)strlen(s), 100);
}

/** 读升级标志 */
static uint16_t Boot_ReadFlag(void)
{
  return *(volatile uint16_t *)FLAG_ADDR;
}

/** 写升级标志（擦标志页后写入） */
static void Boot_WriteFlag(uint16_t flag)
{
  FLASH_EraseInitTypeDef erase = {0};
  uint32_t page_err = 0;

  HAL_FLASH_Unlock();
  erase.TypeErase = FLASH_TYPEERASE_PAGES;
  erase.Banks = FLASH_BANK_1;
  erase.PageAddress = FLAG_PAGE_ADDR;
  erase.NbPages = 1;
  HAL_FLASHEx_Erase(&erase, &page_err);
  HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, FLAG_ADDR, flag);
  HAL_FLASH_Lock();
}

/** 擦 1 页 */
static void Boot_ErasePage(uint32_t addr)
{
  FLASH_EraseInitTypeDef erase = {0};
  uint32_t page_err = 0;

  HAL_FLASH_Unlock();
  erase.TypeErase = FLASH_TYPEERASE_PAGES;
  erase.Banks = FLASH_BANK_1;
  erase.PageAddress = addr;
  erase.NbPages = 1;
  HAL_FLASHEx_Erase(&erase, &page_err);
  HAL_FLASH_Lock();
}

/** 写 Flash（按半字编程） */
static void Boot_ProgramBuf(uint32_t addr, const uint8_t *buf, uint32_t len)
{
  uint32_t i;

  HAL_FLASH_Unlock();
  for (i = 0; i < len; i += 2)
  {
    uint16_t half = buf[i] | ((i + 1 < len) ? (buf[i + 1] << 8) : 0);
    HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, addr + i, half);
  }
  HAL_FLASH_Lock();
}

/** 跳转到 APP（0x08006000） */
static void Boot_JumpToApp(void)
{
  uint32_t app_sp, app_pc;
  void (*entry)(void);

  app_sp = *(volatile uint32_t *)APP_ADDR;
  app_pc = *(volatile uint32_t *)(APP_ADDR + 4);

  __disable_irq();
  SCB->VTOR = APP_ADDR;
  __set_MSP(app_sp);
  entry = (void (*)(void))app_pc;
  entry();

  while (1) { }
}

/* ================= ESP8266 透传配置（仅在升级请求时执行一次） ================= */

/** 等待 USART3 应答中出现关键字，超时返回 0 */
static uint8_t ESP_WaitKey(const char *key, uint32_t timeout_ms)
{
  uint32_t t0 = HAL_GetTick();
  uint16_t n = 0;
  char buf[80];

  memset(buf, 0, sizeof(buf));
  while (HAL_GetTick() - t0 < timeout_ms)
  {
    uint8_t c;
    if (HAL_UART_Receive(&huart3, &c, 1, 30) == HAL_OK)
    {
      if (n < sizeof(buf) - 1)
        buf[n++] = (char)c;
      buf[n] = '\0';
      if (strstr(buf, key) != NULL)
        return 1;
      /* 提前判失败，节省等待时间 */
      if (strstr(buf, "ERROR") != NULL || strstr(buf, "FAIL") != NULL)
        return 0;
    }
  }
  return 0;
}

/** 发送 AT 指令并等待 OK（或指定关键字），返回是否成功 */
static uint8_t ESP_SendAT(const char *cmd, const char *ok_key, uint32_t timeout_ms)
{
  HAL_UART_Transmit(&huart3, (uint8_t *)cmd, (uint16_t)strlen(cmd), 300);
  return ESP_WaitKey(ok_key, timeout_ms);
}

/** 等待 USART3 应答中出现任意一个关键字（兼容 OK / CONNECT 等不同固件应答），超时返回0 */
static uint8_t ESP_WaitKey2(const char *key1, const char *key2, uint32_t timeout_ms)
{
  uint32_t t0 = HAL_GetTick();
  uint16_t n = 0;
  char buf[80];

  memset(buf, 0, sizeof(buf));
  while (HAL_GetTick() - t0 < timeout_ms)
  {
    uint8_t c;
    if (HAL_UART_Receive(&huart3, &c, 1, 30) == HAL_OK)
    {
      if (n < sizeof(buf) - 1)
        buf[n++] = (char)c;
      buf[n] = '\0';
      if (strstr(buf, key1) != NULL || strstr(buf, key2) != NULL)
        return 1;
      if (strstr(buf, "ERROR") != NULL || strstr(buf, "FAIL") != NULL)
        return 0;
    }
  }
  return 0;
}

/** ESP 硬件复位：拉低 RST 引脚 120ms 再释放，等模块重新启动 */
static void ESP_HardReset(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  gpio.Pin   = ESP_RST_GPIO_PIN;
  gpio.Mode  = GPIO_MODE_OUTPUT_PP;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(ESP_RST_GPIO_PORT, &gpio);

  HAL_GPIO_WritePin(ESP_RST_GPIO_PORT, ESP_RST_GPIO_PIN, GPIO_PIN_RESET); /* 拉低复位 */
  HAL_Delay(120);
  HAL_GPIO_WritePin(ESP_RST_GPIO_PORT, ESP_RST_GPIO_PIN, GPIO_PIN_SET);   /* 释放启动 */
  HAL_Delay(500);   /* 等 ESP 启动 */
}

/** 尝试把 ESP8266 配置为透传模式并连上电脑 TCP Server；成功返回1
 *  思路：不重启 ESP（软件/硬件复位实测都不可靠），直接接管 APP 已配好的状态
 *  （已连热点、已设静态 IP），只做：关服务器 -> 切单连接透传 -> 连电脑 */
static uint8_t ESP_TryWiFi(void)
{
  char cmd[96];

  /* 1) 关闭 APP 阶段开的 TCP 服务器（CIPSERVER=1,8080） */
  Boot_Print("[W] CIPSERVER=0...\r\n");
  if (!ESP_SendAT("AT+CIPSERVER=0\r\n", "OK", 2000))
  {
    /* 容错：若服务器已关（回 ERROR）也继续 */
    if (!ESP_SendAT("AT+CIPSERVER=0\r\n", "OK", 2000))
      Boot_Print("[W] CIPSERVER=0 FAIL (continue)\r\n");
  }
  Boot_Print("[W] CIPSERVER=0 OK\r\n");

  /* 2) 确认 AT 通道正常 */
  if (!ESP_SendAT("AT\r\n", "OK", 1500))     { Boot_Print("[W] AT FAIL\r\n"); return 0; }
  Boot_Print("[W] AT OK\r\n");

  /* 3) 保持 Station 模式 + 已设静态 IP + 已连热点，不做复位 */

  /* 4) 切单连接透传（必须 CIPMUX=0） */
  if (!ESP_SendAT("AT+CIPMUX=0\r\n", "OK", 1000))  { Boot_Print("[W] CIPMUX FAIL\r\n"); return 0; }
  Boot_Print("[W] CIPMUX OK\r\n");
  if (!ESP_SendAT("AT+CIPMODE=1\r\n", "OK", 1000)) { Boot_Print("[W] CIPMODE FAIL\r\n"); return 0; }
  Boot_Print("[W] CIPMODE OK\r\n");

  /* 5) ESP 作为 TCP Client 连电脑脚本（<PC_IP>:9000）
     注意：部分固件连上后回 CONNECT 而非 OK，需两者都等，只发一次指令 */
  sprintf(cmd, "AT+CIPSTART=\"TCP\",\"%s\",%s\r\n", ESP_PC_IP, ESP_PC_PORT);
  HAL_UART_Transmit(&huart3, (uint8_t *)cmd, (uint16_t)strlen(cmd), 300);
  if (!ESP_WaitKey2("OK", "CONNECT", 8000))
  {
    Boot_Print("[W] CIPSTART FAIL\r\n");
    return 0;
  }
  Boot_Print("[W] CIPSTART OK\r\n");

  /* 6) 进入透传：之后 TCP 数据 <-> USART3 自动双向转发 */
  if (!ESP_SendAT("AT+CIPSEND\r\n", ">", 2000))
  {
    Boot_Print("[W] CIPSEND FAIL\r\n");
    return 0;
  }
  Boot_Print("[W] CIPSEND OK\r\n");

  return 1;
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  */
void Error_Handler(void)
{
  __disable_irq();
  while (1) { }
}
