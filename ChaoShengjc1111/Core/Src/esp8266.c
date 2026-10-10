#include "esp8266.h"
#include "usart.h"
#include "delay.h"
#include <string.h>
#include <stdio.h>
#include "cmsis_os.h"        /* 新增：任务延时 osDelay 需要 */
#include "ota_app.h"

/* 开源前已移除原始网络凭据。使用前请在此填写自己的网络配置。
 * 静态 IP、网关、掩码必须符合路由器/热点的网段规划；也可根据 ESP AT 固件能力改用 DHCP。
 */
#define OTA_WIFI_SSID       "YOUR_WIFI_SSID"
#define OTA_WIFI_PASSWORD   "YOUR_WIFI_PASSWORD"
#define OTA_ESP_STATIC_IP   "192.168.1.88"
#define OTA_GATEWAY_IP      "192.168.1.1"
#define OTA_NETMASK         "255.255.255.0"

/* ========== 接收缓冲区 768 字节，足够容纳完整 HTTP 请求 ========== */
#define USART_RX_BUF_LEN  768
char usart_rx_buf[USART_RX_BUF_LEN];
uint16_t rx_idx = 0;
uint8_t rx_tmp = 0;   /* HAL串口中断接收的临时字节 */

/* ===== USB-TTL 调试口（USART1 PA9/PA10）接收缓冲，仅用于检测 OTA_START 触发 =====
 * 方案2：ESP8266 已挪到 USART3(PB10/PB11)，USART1 专留给 USB-TTL 做 OTA/调试 */
#define DBG_RX_BUF_LEN  48
char dbg_rx_buf[DBG_RX_BUF_LEN];
uint8_t dbg_idx = 0;
uint8_t dbg_tmp = 0;

/* ========== 手机端控制页面（精简英文版，节省Flash；HTML5宽松语法） ========== */
#define HTML_PAGE "<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'><style>body{font-family:sans-serif;text-align:center;margin:16px}.big{font-size:44px;color:red}input,button{font-size:18px;margin-top:10px;padding:8px}button{background:#07c160;color:#fff;border:0;border-radius:6px}</style><p>Dist:<span class=big id=d>0</span>cm</p><p>Set:<span id=s>0</span>cm</p><input id=in type=number placeholder=Threshold><br><button onclick=setv()>Submit</button><script>function setv(){v=in.value;if(v)fetch('/set?setdist='+v).then(()=>{s.innerText=v;alert('OK')})}setInterval(()=>fetch('/getdist').then(r=>r.text()).then(t=>d.innerText=t),1000)</script>"

/**
  * @brief  通过 USART1（USB-TTL）输出一行诊断日志
  */
static void DBG_Print(const char *s)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)s, (uint16_t)strlen(s), 200);
}

/**
  * @brief  串口中断接收初始化（方案2：双串口）
  * @note   - ESP8266 走 USART3(PB10/PB11)：开启中断接收
  *         - USB-TTL 走 USART1(PA9/PA10)：开启中断接收，用于 OTA_START 触发/调试
  *         串口寄存器配置由 CubeMX 生成的 MX_USARTx_UART_Init() 完成(115200,8N1,中断已使能)
  */
void ESP8266_USART1_Init(void)
{
    HAL_UART_Receive_IT(&huart3, &rx_tmp, 1);    /* ESP8266 数据 -> USART3 */
    HAL_UART_Receive_IT(&huart1, &dbg_tmp, 1);   /* USB-TTL 调试 -> USART1 */
}

/**
  * @brief  串口发送字符串给 ESP8266（USART3）
  */
void ESP8266_SendString(char *str)
{
    HAL_UART_Transmit(&huart3, (uint8_t *)str, strlen(str), 1000);
}

/** 清空接收缓冲区 */
static void ESP8266_ClearRxBuf(void)
{
    memset(usart_rx_buf, 0, USART_RX_BUF_LEN);
    rx_idx = 0;
}

/** 等待缓冲区出现指定关键字，超时返回0 */
static uint8_t ESP8266_WaitFor(const char *key, uint16_t timeout_ms)
{
    while (timeout_ms--)
    {
        if (strstr(usart_rx_buf, key) != NULL)
            return 1;
        delay_ms(1);
    }
    return 0;
}

/** 发送AT指令并等待 OK，出现 ERROR/FAIL 判定失败 */
static uint8_t ESP8266_SendAT_OK(const char *at, uint16_t timeout_ms)
{
    ESP8266_ClearRxBuf();
    ESP8266_SendString((char *)at);
    while (timeout_ms--)
    {
        if (strstr(usart_rx_buf, "OK") != NULL)    return 1;
        if (strstr(usart_rx_buf, "ERROR") != NULL) return 0;
        if (strstr(usart_rx_buf, "FAIL") != NULL)  return 0;
        delay_ms(1);
    }
    return 0;
}

/**
  * @brief  上电自动配置（诊断版）：每条AT结果通过USART1打印，CIPSERVER失败重试
  */
void ESP8266_AutoConfigAT(void)
{
    char line[96];
    char at_cmd[160];
    uint8_t r;
    int retry;

    ESP8266_ClearRxBuf();
    ESP8266_SendString("AT+RST\r\n");
    ESP8266_WaitFor("ready", 3000);
    delay_ms(500);

    r = ESP8266_SendAT_OK("AT\r\n", 1000);
    DBG_Print(r ? "[1] AT -> OK\r\n" : "[1] AT -> FAIL\r\n");

    r = ESP8266_SendAT_OK("AT+CWMODE=1\r\n", 1000);
    DBG_Print(r ? "[2] CWMODE -> OK\r\n" : "[2] CWMODE -> FAIL\r\n");

    /* 设置静态 IP。请按当前网络修改上方 OTA_ESP_STATIC_IP / OTA_GATEWAY_IP / OTA_NETMASK。 */
    snprintf(at_cmd, sizeof(at_cmd), "AT+CIPSTA=\"%s\",\"%s\",\"%s\"\r\n",
             OTA_ESP_STATIC_IP, OTA_GATEWAY_IP, OTA_NETMASK);
    r = ESP8266_SendAT_OK(at_cmd, 1500);
    DBG_Print(r ? "[2.5] CIPSTA -> OK\r\n" : "[2.5] CIPSTA -> FAIL(检查网络配置)\r\n");

    /* 请自行填写 OTA_WIFI_SSID 与 OTA_WIFI_PASSWORD；切勿提交真实密码。 */
    snprintf(at_cmd, sizeof(at_cmd), "AT+CWJAP=\"%s\",\"%s\"\r\n",
             OTA_WIFI_SSID, OTA_WIFI_PASSWORD);
    r = ESP8266_SendAT_OK(at_cmd, 12000);
    DBG_Print(r ? "[3] CWJAP -> OK\r\n" : "[3] CWJAP -> TIMEOUT(可能仍在连)\r\n");
    delay_ms(800);                 /* 联网后等待 IP/网络稳定 */

    r = ESP8266_SendAT_OK("AT+CIPMUX=1\r\n", 1500);
    DBG_Print(r ? "[4] CIPMUX -> OK\r\n" : "[4] CIPMUX -> FAIL\r\n");

    /* CIPSERVER 重试 3 次，确保 8080 服务器建立 */
    for (retry = 0; retry < 3; retry++)
    {
        r = ESP8266_SendAT_OK("AT+CIPSERVER=1,8080\r\n", 1500);
        sprintf(line, "[5] CIPSERVER 第%d次 -> %s\r\n", retry + 1, r ? "OK" : "FAIL");
        DBG_Print(line);
        if (r) break;
        delay_ms(500);
    }

    /* 查询并通过 USART1 打印 ESP 真实 IP */
    ESP8266_ClearRxBuf();
    ESP8266_SendString("AT+CIFSR\r\n");
    ESP8266_WaitFor("OK", 1500);
    DBG_Print("----- ESP CIFSR -----\r\n");
    DBG_Print(usart_rx_buf);
    DBG_Print("\r\n--------------------\r\n");

    ESP8266_ClearRxBuf();
}

/** 从 "+IPD,通道号,长度:" 提取通道号 */
static int ESP8266_GetLinkID(char *buf)
{
    int id = 0;
    char *p = strstr(buf, "+IPD,");
    if (p == NULL) return -1;
    p += 5;
    while (*p >= '0' && *p <= '9')
    {
        id = id * 10 + (*p - '0');
        p++;
    }
    return id;
}

/** 通过 AT+CIPSEND 发送HTTP响应，并关闭该通道连接 */
static void ESP8266_SendHTTPResponse(int link_id, const char *body)
{
    char cmd[64];
    char header[96];
    char lenstr[8];
    uint16_t len;
    uint16_t total;

    len = strlen(body);
    sprintf(lenstr, "%d", len);

    /* 组装 HTTP 响应头（统一用 text/html，纯文本数字也能正常显示） */
    sprintf(header, "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: %s\r\nConnection: close\r\n\r\n", lenstr);
    total = strlen(header) + len;

    ESP8266_ClearRxBuf();
    sprintf(cmd, "AT+CIPSEND=%d,%d\r\n", link_id, total);
    ESP8266_SendString(cmd);

    if (ESP8266_WaitFor(">", 2000))
    {
        ESP8266_SendString(header);           /* 先发 HTTP 响应头 */
        ESP8266_SendString((char *)body);     /* 再发响应内容 */
        /* 等 ESP 返回 SEND OK 确认数据完整发出，再关连接，
           避免提前关闭导致客户端收到空响应/连接重置 */
        ESP8266_WaitFor("SEND OK", 3000);
        osDelay(pdMS_TO_TICKS(20));
    }

    /* 数据确认发出后再关闭 TCP 连接，释放 ESP8266 连接资源（最多 5 个） */
    sprintf(cmd, "AT+CIPCLOSE=%d\r\n", link_id);
    ESP8266_SendString(cmd);
    osDelay(pdMS_TO_TICKS(30));
    ESP8266_ClearRxBuf();
}

/**
  * @brief  处理ESP8266接收到的数据，响应浏览器请求
  * @param  cur_dist     当前实测距离(cm)
  * @param  set_dist     当前设定值(距离阈值)
  * @param  out_set_dist 输出：浏览器设置的新距离阈值
  * @retval 1 = 收到新的设定值; 0 = 无
  */
uint8_t ESP8266_ProcessESPData(uint16_t cur_dist, uint16_t set_dist, uint16_t *out_set_dist)
{
    char *p;
    int link_id;

    /* 缓冲区没有 +IPD：可能是AT响应或请求数据未收完。
       关键修复：数据正在接收时绝不能清空，否则半截 +IPD 被丢弃，
       请求永远凑不齐 -> 空响应/超时。采用"静默超时"清除残留AT响应 */
    if (strstr(usart_rx_buf, "+IPD") == NULL)
    {
        static uint16_t last_rx = 0;
        static uint8_t  idle_cnt = 0;
        if (rx_idx == last_rx)          /* 无新字节到达 */
        {
            if (++idle_cnt >= 5)        /* 静默约25ms后清残留(AT响应等) */
            {
                ESP8266_ClearRxBuf();
                idle_cnt = 0;
            }
        }
        else                            /* 有新字节正在接收，不清！ */
        {
            last_rx = rx_idx;
            idle_cnt = 0;
        }
        return 0;
    }

    /* HTTP 以 \r\n\r\n 结尾，若没有说明数据未接收完整，等待 */
    if (strstr(usart_rx_buf, "\r\n\r\n") == NULL)
    {
        if (rx_idx >= USART_RX_BUF_LEN - 2)   /* 缓冲区已满还没收完 = 异常 */
            ESP8266_ClearRxBuf();
        return 0;
    }

    /* 提取连接通道号 */
    link_id = ESP8266_GetLinkID(usart_rx_buf);
    if (link_id < 0)
    {
        ESP8266_ClearRxBuf();
        return 0;
    }

    /* 0) 浏览器访问根路径 GET /  -> 返回手机控制页面 */
    if (strstr(usart_rx_buf, "GET / ") != NULL || strstr(usart_rx_buf, "GET /?") != NULL)
    {
        ESP8266_SendHTTPResponse(link_id, "CSR Server OK");
        return 0;
    }

    /* 0.5) favicon：浏览器标签页自动请求，回空内容，避免它走到默认分支回 "OK" 干扰页面 */
    if (strstr(usart_rx_buf, "GET /favicon") != NULL)
    {
        ESP8266_SendHTTPResponse(link_id, "");
        return 0;
    }

    /* 1) 获取实时距离 GET /getdist -> 返回当前距离 */
    if (strstr(usart_rx_buf, "GET /getdist") != NULL)
    {
        char body[8];
        sprintf(body, "%d", cur_dist);
        ESP8266_SendHTTPResponse(link_id, body);
        return 0;
    }

    /* 2) 查询当前设定值 GET /getset -> 返回 set_dist */
    if (strstr(usart_rx_buf, "GET /getset") != NULL)
    {
        char body[8];
        sprintf(body, "%d", set_dist);
        ESP8266_SendHTTPResponse(link_id, body);
        return 0;
    }

    /* 3) 设置距离值 GET /set?setdist=xx 或 GET /set?val=xx（兼容两种参数名） */
    p = strstr(usart_rx_buf, "setdist=");
    if (p == NULL)
        p = strstr(usart_rx_buf, "val=");
    if (p != NULL)
    {
        uint16_t num = 0;
        p = strchr(p, '=') + 1;             /* 跳到等号后取数字 */
        while (*p >= '0' && *p <= '9')
        {
            num = num * 10 + (*p - '0');
            p++;
        }
        if (num > 200) num = 200;
        *out_set_dist = num;

        ESP8266_SendHTTPResponse(link_id, "SET OK");   /* 回包确认 */
        return 1;
    }

    /* 5) OTA升级指令 GET /ota -> 写升级标志并复位进入Bootloader */
    if (strstr(usart_rx_buf, "GET /ota") != NULL)
    {
        ESP8266_SendHTTPResponse(link_id, "OTA START");
        OTA_TriggerReboot();
        return 0;
    }

    /* 4) 其他未知请求，返回默认响应 */
    ESP8266_SendHTTPResponse(link_id, "OK");
    return 0;
}

/**
  * @brief  串口接收中断回调（USARTx_IRQHandler 由 CubeMX 生成并自动调用 HAL_UART_IRQHandler）
  * @note   方案2 双串口分流：
  *           USART3(PB10/PB11) -> ESP8266 数据，存入 usart_rx_buf
  *           USART1(PA9/PA10)  -> USB-TTL，检测 OTA_START 触发升级
  */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART3)
    {
        /* ---- ESP8266 数据 ---- */
        if (rx_idx < USART_RX_BUF_LEN - 1)
        {
            usart_rx_buf[rx_idx++] = rx_tmp;
            usart_rx_buf[rx_idx] = '\0';
        }
        HAL_UART_Receive_IT(&huart3, &rx_tmp, 1);   /* 继续接收下一字节 */
    }
    else if (huart->Instance == USART1)
    {
        /* ---- USB-TTL 调试口：检测 OTA_START ---- */
        if (dbg_idx >= DBG_RX_BUF_LEN - 1)
            dbg_idx = 0;                             /* 缓冲满回绕，保证后续 OTA_START 可检测 */

        dbg_rx_buf[dbg_idx++] = dbg_tmp;
        dbg_rx_buf[dbg_idx] = '\0';

        /* USB-TTL 直发 OTA_START -> 写升级标志并复位进 Bootloader */
        if (strstr(dbg_rx_buf, "OTA_START") != NULL)
        {
            OTA_TriggerReboot();
        }
        HAL_UART_Receive_IT(&huart1, &dbg_tmp, 1);  /* 继续接收下一字节 */
    }
}
