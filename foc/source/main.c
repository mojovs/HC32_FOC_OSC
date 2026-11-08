/**
 *******************************************************************************
 * @file  foc/source/main.c
 * @brief HC32F460KETA + DRV8301 FOC 固件 - 主函数
 *
 * 对照 STM32F4 原工程的主函数 main():
 *   配套代码/代码/code/user/main.c
 *
 * 本工程 main():                          说明:
 *   hard_init();                            板级初始化: 时钟 200MHz/USB PLLX
 *                                           + GPIO/SPI/ADC/PWM/霍尔捕获/
 *                                           按键 EXTI (详见 board_config.c)
 *   OLED_Init();                            待移植 (本板 OLED 走 I2C PC7/PC8,
 *                                           与 DRV8301 的 SPI 引脚不冲突)
 *   drv8301_init();                         待移植 (motor/drv8301.c)
 *   pc_communication_init();                放在 usb_dev_init() 之前调用
 *                                           (USB CDC 协议引擎; USART3 非阻塞
 *                                           + CDC<->CN1 桥接 + RX 环形缓冲)
 *   foc_algorithm_initialize();             待移植 (motor/foc_*.c)
 *   if (get_offset_flag==0) {               PWM 输出使能由 hard_init 里的
 *     get_offset_flag=1;                    PwmInit() 完成; 此处再拉高
 *     TIM_CtrlPWMOutputs(ENABLE);           EN_GATE 后才会有输出, 之后
 *   }                                       drv8301_init() 才能生效
 *   while(1) {                              主循环: vcp_task()/USB 事件/ADC/1ms
 *     drv8301_protection();                 待移植 drv8301_protection()
 *     oled_display_handle();                待移植 oled_display_handle()
 *   }
 *
 * 本工程说明 (由 usb_dev_cdc + usart_uart_int 合并而来):
 *   - USB CDC 与 CN1 (USART3) 双向桥接;
 *   - board -> PC 的数据统一由 vcp_send() 追加进 IN 环 (含发往 PC 的文本);
 *   - CN1 收到的字节写入接收环形缓冲, 由 UART_ReadByte() 逐字节取用,
 *     在非中断环境处理 (usart_uart_int 的做法);
 *   - printf 走 USART3 TX (CN1); USB 与 USART 两路互不阻塞,
 *     便于调试与协议并行。
 *
 * hard_init() 完成后进入主循环, LED 以 1Hz 心跳闪烁, 指示系统运行中。
 *******************************************************************************
 * Copyright (C) 2022-2025, Xiaohua Semiconductor Co., Ltd. All rights reserved.
 *
 * This software component is licensed by XHSC under BSD 3-Clause license
 * (the "License"); You may not use this file except in compliance with the
 * License. You may obtain a copy of the License at:
 *                    opensource.org/licenses/BSD-3-Clause
 *
 *******************************************************************************
 */

/*******************************************************************************
 * 包含的头文件
 ******************************************************************************/
#include "main.h"
#include "board_config.h"
#include "cdc_data_process.h"
#include "pc_communication.h"
#include "pc_communication_init.h"
#include <stdio.h>

/**
 * @addtogroup HC32F460_DDL_Examples
 * @{
 */

/**
 * @addtogroup FOC
 * @{
 */

/*******************************************************************************
 * 宏定义
 ******************************************************************************/
/* 外设写保护掩码: 覆盖后面需要改的 GPIO/FCG/PWC/EFM/SRAM。
   注意: 若在 LL_PERIPH_WP() 之后才调用 CLK_SetSysClockSrc(PLL)
   或写 PWC_FCG0PC.PRT0, 会触发 DDL_AssertHandler() 断言。 */
#define LL_PERIPH_SEL                   (LL_PERIPH_GPIO | LL_PERIPH_FCG | LL_PERIPH_PWC_CLK_RMU | \
                                         LL_PERIPH_EFM | LL_PERIPH_SRAM)

/* 写保护后仍保留 GPIO 可写: 便于调试点灯 PC9 (见 usb_dev_cdc)。 */
#define LL_PERIPH_WP_SEL                (LL_PERIPH_EFM | LL_PERIPH_PWC_CLK_RMU | LL_PERIPH_SRAM)

/* LED 翻转周期, 单位 ms */
#define LED_TOGGLE_MS                   (500UL)

/*******************************************************************************
 * 全局变量
 ******************************************************************************/
/* USB 设备实例 (供 usb_dev_init/事件回调引用)。 */
usb_core_instance usb_dev;

/*******************************************************************************
 * 本地函数声明 ('static')
 ******************************************************************************/
static void CompanionModulesInit(void);
static void UsbEventTask(void);

/*******************************************************************************
 * 函数实现 - 全局 ('extern') 与本地 ('static')
 ******************************************************************************/

/**
 * @brief  由 main() 调用的配套模块初始化汇总。
 *         目前只初始化 PC 上位机链路; 其余模块见下方 TODO。
 */
static void CompanionModulesInit(void)
{
    /* PC host tool link: reset the engine and register all channels. */
    pc_communication_init();
    /* OLED_Init();                    - TODO: 待移植 OLED 驱动 (I2C, PC7/PC8),
                                         与 DRV8301 的 SPI 引脚不冲突 */
    /* drv8301_init();                 - TODO: 移植 motor/drv8301.c, 通过 SPI
                                         配置 EN_GATE/电流放大/故障等寄存器
                                         (依赖 get_offset_flag) */
    /* pc_communication_init();        - 已移到 usb_dev_init() 之前: USB CDC
                                         需先完成枚举再供 PC 上位机通信;
                                         vcp_send()/UART_ReadByte() 已就绪 */
    /* foc_algorithm_initialize();     - TODO: 移植 motor/foc_algorithm.c */
}

/**
 * @brief  处理 USB 事件 (由 usb_dev_user.c 的事件回调置位)。
 *         printf 输出到 CN1, 不影响 USB 数据流 (见 usb_dev_cdc);
 *         真正的协议解析在 communication_handle() 里: PC 命令按需解析。
 */
static void UsbEventTask(void)
{
#if (VCP_BRIDGE_TO_UART != 0U)
    static uint8_t m_au8Greeting[] =
        "\r\nSMotorDrive foc firmware: VCP ready.\r\n"
        "USB CDC <-> CN1 (USART3) bridged; commands pending pc_comm port.\r\n";
#endif
    uint8_t u8Evt = g_u8UsbEvt;

    if (0U == u8Evt) {
        return;
    }
    if (g_u8UsbEvt == u8Evt) {
        g_u8UsbEvt = 0U;
    }

    switch (u8Evt) {
        case USB_EVT_RESET:
            printf(">> USB bus reset\r\n");
            break;
        case USB_EVT_CONFIG:
            printf(">> USB configured, CDC ready (COM port open)\r\n");
#if (VCP_BRIDGE_TO_UART != 0U)
/* 把问候语追加进 CDC IN 环形缓冲, 由 USB 发往 PC 终端。 */
            vcp_send(m_au8Greeting, (uint32_t)(sizeof(m_au8Greeting) - 1U));
#endif
            break;
        case USB_EVT_SUSPEND:
            printf(">> USB suspend\r\n");
            break;
        case USB_EVT_RESUME:
            printf(">> USB resume\r\n");
            break;
        case USB_EVT_DISCONN:
            printf(">> USB disconnected\r\n");
            break;
        default:
            break;
    }
}

/**
 * @brief  FOC 固件主入口 (永不返回)
 * @param  None
 * @retval int32_t return value, if needed
 */
int32_t main(void)
{
    stc_usb_port_identify stcPortIdentify;

/* 先打开外设写保护, 允许后续初始化寄存器 */
    LL_PERIPH_WE(LL_PERIPH_SEL);

/* 板级初始化: 时钟 200MHz + USB PLLX 48MHz, GPIO, DRV8301 SPI, ADC,
   20kHz PWM, 霍尔捕获, 按键 EXTI。 */
    hard_init();

    CompanionModulesInit();

/* USB CDC 初始化 (必须在 pc_communication_init() 之前):
   usb_dev_init() -> usb_bsp_init() (DP/DM 引脚, USB 时钟, USART3 引脚)
   -> 中断/FIFO/端点 (INT030, 与 USB 事件) -> CDC 驱动 vcp_init()
   (USART3 配置 + 接收中断), 完成枚举后 PC 才能通信。 */
    stcPortIdentify.u8CoreID = USBFS_CORE_ID;
    usb_dev_init(&usb_dev, &stcPortIdentify, &user_desc, &class_cdc_cbk, &user_cb);

/* 初始化完成后加回写保护:
   GPIO/FCG 等不可再改 (防误写, 仅保留 EFM/PWC/SRAM 可写)。 */
    LL_PERIPH_WP(LL_PERIPH_WP_SEL);

    printf("\r\n========================================\r\n");
    printf(" SMotorDrive foc: board init done\r\n");
    printf(" 200MHz, 20kHz PWM, USB CDC + USART3\r\n");
    printf(" CN1: 2=PB7(RX), 3=PB6(TX), 115200-8-N-1\r\n");
    printf("========================================\r\n");

    for (;;) {
/* CDC(OUT) -> USART3 TX 的排空任务, 在主循环里做 (USB -> CN1 方向)。 */
        vcp_task();
        communication_send_task();   /* flush a finished PC host wave frame */

/* USB 事件处理 (协议解析在 communication_handle 里)。 */
        UsbEventTask();

/* ADC 序列 A 已改由 TMRA_1 OVF 事件经 AOS 硬件触发, 结果由 DMA 搬到
   g_au16AdcValue, 块完成中断里跑 communication_task(); 主循环不再轮询 EOCA。 */

/* 1ms 节拍 (原工程用 SysTick_Handler/TimingDelay_Decrement;
   本工程由 board_config.c 的 PWM 计频派生) - TODO: 周期调用 drv8301_protection() */
        if (g_u8Tick1ms != 0U) {
            static uint32_t s_u32Ms = 0UL;
            static uint32_t s_u32PcCmdCnt = 0UL;
            s_u32Ms++;

            /* PC host commands are parsed at 100 Hz (1 ms tick / 10). */
            if ((++s_u32PcCmdCnt % 10UL) == 0UL) {
                communication_handle();
            }

/* 按键消抖与处理 (消抖结束后重新使能对应 EXTI 中断) */
            Board_Key1msTask();

            if ((s_u32Ms % LED_TOGGLE_MS) == 0UL) {
                GPIO_TogglePins(USER_LED_PORT, USER_LED_PIN);   /* 心跳翻转 */
            }
            g_u8Tick1ms = 0U;
        }

        /* TODO: drv8301_protection(); oled_display_handle(); */
    }
}

/**
 * @}
 */

/**
 * @}
 */

/*******************************************************************************
 * EOF (not truncated)
 ******************************************************************************/
