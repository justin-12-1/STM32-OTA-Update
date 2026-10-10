/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "key.h"
#include "hc_sr04.h"
#include "oled.h"
#include "beep.h"
#include "led.h"
#include "store.h"
#include "esp8266.h"
#include "delay.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
extern volatile int distance;    /* 在 hc_sr04.c 中定义 */
extern int s_distance;
extern int c_distance;
extern uint8_t edit_mode;
extern int key_num;
/* USER CODE END Variables */
/* Definitions for Task_key */
osThreadId_t Task_keyHandle;
const osThreadAttr_t Task_key_attributes = {
  .name = "Task_key",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for Task_Sensor */
osThreadId_t Task_SensorHandle;
const osThreadAttr_t Task_Sensor_attributes = {
  .name = "Task_Sensor",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal1,
};
/* Definitions for Task_OLED */
osThreadId_t Task_OLEDHandle;
const osThreadAttr_t Task_OLED_attributes = {
  .name = "Task_OLED",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for Task_EspProcess */
osThreadId_t Task_EspProcessHandle;
const osThreadAttr_t Task_EspProcess_attributes = {
  .name = "Task_EspProcess",
  .stack_size = 768 * 4,
  .priority = (osPriority_t) osPriorityAboveNormal,
};
/* Definitions for espQueue */
osMessageQueueId_t espQueueHandle;
const osMessageQueueAttr_t espQueue_attributes = {
  .name = "espQueue"
};
/* Definitions for dataMutex */
osMutexId_t dataMutexHandle;
const osMutexAttr_t dataMutex_attributes = {
  .name = "dataMutex"
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartTask_key(void *argument);
void StartTask_Sensor(void *argument);
void StartTask_OLED(void *argument);
void StartTask_EspProces(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */
  /* Create the mutex(es) */
  /* creation of dataMutex */
  dataMutexHandle = osMutexNew(&dataMutex_attributes);

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* Create the queue(s) */
  /* creation of espQueue */
  espQueueHandle = osMessageQueueNew (128, sizeof(uint8_t), &espQueue_attributes);

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of Task_key */
  Task_keyHandle = osThreadNew(StartTask_key, NULL, &Task_key_attributes);

  /* creation of Task_Sensor */
  Task_SensorHandle = osThreadNew(StartTask_Sensor, NULL, &Task_Sensor_attributes);

  /* creation of Task_OLED */
  Task_OLEDHandle = osThreadNew(StartTask_OLED, NULL, &Task_OLED_attributes);

  /* creation of Task_EspProcess */
  Task_EspProcessHandle = osThreadNew(StartTask_EspProces, NULL, &Task_EspProcess_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartTask_key */
/**
  * @brief  Function implementing the Task_key thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartTask_key */
void StartTask_key(void *argument)
{
  /* USER CODE BEGIN StartTask_key */
  /* Infinite loop */
	for(;;)
	  {
	    key_num = read_key16_value();
	    if (key_num != -1)
	    {
	      osDelay(pdMS_TO_TICKS(10));                /* 消抖（RTOS延时） */
	      key_num = read_key16_value();
	      if (key_num != -1)
	      {
	        osMutexAcquire(dataMutexHandle, osWaitForever);
	        if (key_num == 15)                       /* S16 进入编辑模式 */
	        {
	          edit_mode = 1;
	          c_distance = 0;
	        }
	        if (edit_mode == 1)
	        {
	          if (key_num >= 0 && key_num <= 9)      /* 数字键 */
	          {
	            c_distance = c_distance * 10 + key_num;
	            if (c_distance > 99) c_distance = 99;
	          }
	          else if (key_num == 12)                /* S13 确认保存 */
	          {
	            if (c_distance > 100) c_distance = 100;
	            if (c_distance < 0)  c_distance = 0;
	            s_distance = c_distance;
	            edit_mode = 0;
	            Store_Data[1] = (uint16_t)s_distance;
	            Store_Save();                        /* Flash保存（锁内，避免与ESP任务并发擦写） */
	          }
	          else if (key_num == 14)                /* S15 清空 */
	          {
	            c_distance = 0;
	          }
	        }
	        osMutexRelease(dataMutexHandle);

	        while (read_key16_value() != -1) osDelay(1);  /* 等松开，让出CPU */
	      }
	    }
	    osDelay(pdMS_TO_TICKS(10));
	  }
  /* USER CODE END StartTask_key */
}

/* USER CODE BEGIN Header_StartTask_Sensor */
/**
* @brief Function implementing the Task_Sensor thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTask_Sensor */
void StartTask_Sensor(void *argument)
{
  /* USER CODE BEGIN StartTask_Sensor */
  /* Infinite loop */
	for(;;)
	  {
	    int d = get_distance();                      /* Trig 10us 用DWT，回波等待忙等~30ms可接受 */
	    osMutexAcquire(dataMutexHandle, osWaitForever);
	    distance = d;                                /* 更新共享距离 */
	    int s = s_distance;
	    osMutexRelease(dataMutexHandle);

	    if (d < 0 || d >= s)                         /* 测量失败或超阈值 */
	    {
	      Led_On();
	      Beep_ShortBeep();
	    }
	    else
	    {
	      Led_Off();
	      Beep_Off();
	    }
	    osDelay(pdMS_TO_TICKS(100));                 /* 100ms 测距周期 */
	  }
  /* USER CODE END StartTask_Sensor */
}

/* USER CODE BEGIN Header_StartTask_OLED */
/**
* @brief Function implementing the Task_OLED thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTask_OLED */
void StartTask_OLED(void *argument)
{
  /* USER CODE BEGIN StartTask_OLED */
  /* Infinite loop */
	for(;;)
	  {
	    osMutexAcquire(dataMutexHandle, osWaitForever);
	    int d = distance;
	    int s = s_distance;
	    int c = c_distance;
	    uint8_t em = edit_mode;
	    osMutexRelease(dataMutexHandle);

	    OLED_ShowString(2, 5, "  ");                 /* 设定值 */
	    OLED_ShowNum(2, 5, s, 2);

	    OLED_ShowString(3, 5, "  ");                 /* 编辑值 */
	    if (em == 1) OLED_ShowNum(3, 5, c, 2);

	    if (d < 0 || d >= s)                         /* 距离显示 */
	    {
	      OLED_ShowString(4, 6, "Error");
	    }
	    else
	    {
	      OLED_ShowString(4, 6, "     ");
	      OLED_ShowNum(4, 6, (uint32_t)d, 2);
	      OLED_ShowString(4, 11, "cm ");
	    }
	    osDelay(pdMS_TO_TICKS(50));                  /* 50ms 刷新一次 */
	  }
  /* USER CODE END StartTask_OLED */
}

/* USER CODE BEGIN Header_StartTask_EspProces */
/**
* @brief Function implementing the myTask04 thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTask_EspProces */
void StartTask_EspProces(void *argument)
{
  /* USER CODE BEGIN StartTask_EspProces */
  /* Infinite loop */
	uint16_t web_new_dist;
	for(;;)
	{
		osMutexAcquire(dataMutexHandle, osWaitForever);
		int d = distance;
		int s = s_distance;
		osMutexRelease(dataMutexHandle);

		if (ESP8266_ProcessESPData((uint16_t)(d > 0 ? d : 0), (uint16_t)s, &web_new_dist) == 1)
		{
			osMutexAcquire(dataMutexHandle, osWaitForever);
			s_distance = web_new_dist;               /* 网页下发新阈值 */
			Store_Data[1] = s_distance;
			Store_Save();
			osMutexRelease(dataMutexHandle);
		}
		osDelay(pdMS_TO_TICKS(5));                  /* 10ms轮询网页请求 */
	}
  /* USER CODE END StartTask_EspProces */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

