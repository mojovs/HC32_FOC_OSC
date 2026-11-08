/**
 *******************************************************************************
 * @file  usb_dev_cdc/source/usb_bsp.c
 * @brief BSP glue for the USB CDC example on the SMotorDrive board.
 *
 * Differences to the vendor example (ev_hc32f460_lqfp100_v2):
 *   1. No BSP_CLK_Init(): the vendor BSP assumes an 8 MHz crystal and would
 *      configure the MPLL for 400 MHz on this 16 MHz board. The clock tree is
 *      set up once in main.c (SystemClockInit).
 *   2. No VBUS pin. PA9 is PWM_BH here, so VBUS sensing is compiled out
 *      (usb_app_conf.h) and GVBUSCFG.VBUSOVEN/VBUSVAL are used to tell the
 *      core that VBUS is valid.
 *   3. No BSP_LED_Init()/BSP_KEY_Init(): PD3..PD6 / PE6 do not exist on
 *      LQFP64. The board LED (PC9) is handled in main.c.
 *   4. Console (DDL_Printf) is USART3 on PB6/PB7 instead of USART4/PE6.
 *
 * Ported from the XHSC DDL example
 *   projects/ev_hc32f460_lqfp100_v2/applications/usb/usb_dev_cdc
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
 * Include files
 ******************************************************************************/
#include "usb_bsp.h"
#include "usb_dev_int.h"
#include "hc32_ll.h"

/**
 * @addtogroup SMotorDrive_USB_Dev_Cdc
 * @{
 */

/*******************************************************************************
 * Local pre-processor symbols/macros ('#define')
 ******************************************************************************/
/* USB FS core: USB-C connector USB1 -> PA11 = DM, PA12 = DP */
#define USB_DP_PORT     (GPIO_PORT_A)
#define USB_DP_PIN      (GPIO_PIN_12)
#define USB_DM_PORT     (GPIO_PORT_A)
#define USB_DM_PIN      (GPIO_PIN_11)

/* printf console: USART3 on CN1 (PB6 TX, PB7 RX), 115200-8-N-1 */
#define PRINT_UNIT      (CM_USART3)
#define PRINT_FCG_ENABLE()      (FCG_Fcg1PeriphClockCmd(FCG1_PERIPH_USART3, ENABLE))
#define PRINT_TX_PORT   (GPIO_PORT_B)
#define PRINT_TX_PIN    (GPIO_PIN_06)
#define PRINT_TX_FUNC   (GPIO_FUNC_32)
#define PRINT_BAUDRATE  (115200UL)
/*
 * LL_PrintfInit() derives its TX-empty timeout as (HCLK / u32Param) loop
 * iterations and also forwards u32Param to the preinit callback. Passing the
 * real baud rate gives only ~one byte time at 200 MHz, which is marginal and
 * can drop characters, so a smaller divisor is passed (longer timeout) and
 * PrintPreinit() uses PRINT_BAUDRATE instead of its argument.
 */
#define PRINT_TIMEOUT_PARAM (PRINT_BAUDRATE / 16UL)

/*******************************************************************************
 * Local function prototypes ('static')
 ******************************************************************************/
static int32_t PrintPreinit(void *vpDevice, uint32_t u32Param);

/*******************************************************************************
 * Function implementation - global ('extern') and local ('static')
 ******************************************************************************/
extern usb_core_instance usb_dev;

/**
 * @brief  handle the USB interrupt
 * @param  None
 * @retval None
 */
static void USB_IRQ_Handler(void)
{
    usb_isr_handler(&usb_dev);
}

/**
 * @brief  Console pre-initialisation for DDL_PrintfInit().
 *         PCLK1 = 100 MHz -> USART_CLK_DIV64 (same as drv8301_test).
 * @param  [in] vpDevice    USART instance (unused, fixed to USART3)
 * @param  [in] u32Param    timeout divisor, not the baud rate (see above)
 * @retval LL_OK / LL_ERR
 */
static int32_t PrintPreinit(void *vpDevice, uint32_t u32Param)
{
    stc_usart_uart_init_t stcUartInit;

    (void)vpDevice;
    (void)u32Param;

    GPIO_SetFunc(PRINT_TX_PORT, PRINT_TX_PIN, PRINT_TX_FUNC);
    PRINT_FCG_ENABLE();

    (void)USART_UART_StructInit(&stcUartInit);
    stcUartInit.u32ClockDiv      = USART_CLK_DIV64;     /* PCLK1 = 100 MHz */
    stcUartInit.u32Baudrate      = PRINT_BAUDRATE;
    stcUartInit.u32OverSampleBit = USART_OVER_SAMPLE_8BIT;
    /* Neither RTS nor CTS is routed on CN1. USART_UART_Init() only programs
       CR3.CTSE for the CTS value, so RTS here == no hardware flow control.
       (The DDL assertion rejects 0UL.) */
    stcUartInit.u32HWFlowControl = USART_HW_FLOWCTRL_RTS;
    if (LL_OK != USART_UART_Init(PRINT_UNIT, &stcUartInit, NULL)) {
        return LL_ERR;
    }

    /* TX only: RX + RX interrupt are owned by the CDC bridge (vcp_init). */
    USART_FuncCmd(PRINT_UNIT, USART_TX, ENABLE);

    return LL_OK;
}

/**
 * @brief  initialize configurations for the BSP
 * @param  [in] pdev                device instance
 * @param  [in] pstcPortIdentify    usb core and phy select
 * @retval None
 */
void usb_bsp_init(usb_core_instance *pdev, stc_usb_port_identify *pstcPortIdentify)
{
    stc_gpio_init_t stcGpioCfg;

    /* Not used by this single-core configuration, kept for API compatibility. */
    (void)pdev;
    (void)pstcPortIdentify;

    /* Console first, so the USB stack can report problems via printf.
       Note: the second argument is the timeout divisor, not the baud rate. */
    DDL_PrintfInit(PRINT_UNIT, PRINT_TIMEOUT_PARAM, &PrintPreinit);

    /* D+/D- are driven by the internal FS PHY: analog, no pull-up/pull-down.
       The internal DP pull-up (DCTL.SDIS) is enabled by usb_devmodeinit(). */
    (void)GPIO_StructInit(&stcGpioCfg);
    stcGpioCfg.u16PinAttr = PIN_ATTR_ANALOG;
    (void)GPIO_Init(USB_DM_PORT, USB_DM_PIN, &stcGpioCfg);
    (void)GPIO_Init(USB_DP_PORT, USB_DP_PIN, &stcGpioCfg);

    /* USB FS clock source = PLLXP = 48 MHz (PLLX itself is set up in main). */
    CLK_SetUSBClockSrc(CLK_USBCLK_PLLXP);
    FCG_Fcg1PeriphClockCmd(FCG1_PERIPH_USBFS, ENABLE);

    /*
     * This board does not route VBUS to a USBFS pin (PA9 = PWM_BH), and
     * VBUS_SENSING_ENABLED is not defined. Force the internal VBUS comparator
     * to "valid" so the core does not consider the session invalid.
     */
    SET_REG32_BIT(CM_USBFS->GVBUSCFG, (USBFS_GVBUSCFG_VBUSOVEN | USBFS_GVBUSCFG_VBUSVAL));
}

/**
 * @brief  configure the NVIC of USB
 * @param  [in] pdev                    device instance
 * @retval None
 */
void usb_bsp_nvicconfig(usb_core_instance *pdev)
{
    stc_irq_signin_config_t stcIrqRegiConf;

    (void)pdev;

    /* Register INT_SRC_USBFS_GLB Int to Vect.No.030 */
    stcIrqRegiConf.enIRQn = INT030_IRQn;
    stcIrqRegiConf.enIntSrc = INT_SRC_USBFS_GLB;
    stcIrqRegiConf.pfnCallback = &USB_IRQ_Handler;
    (void)INTC_IrqSignIn(&stcIrqRegiConf);

    NVIC_ClearPendingIRQ(stcIrqRegiConf.enIRQn);
    /*
     * Lowest priority: when this project is merged into the motor firmware the
     * ADC/PWM/control interrupts must be able to preempt the USB stack.
     */
    NVIC_SetPriority(stcIrqRegiConf.enIRQn, DDL_IRQ_PRIO_DEFAULT);
    NVIC_EnableIRQ(stcIrqRegiConf.enIRQn);
}

/**
 * @brief  This function provides delay time in micro sec
 * @param  [in] usec        Value of delay required in micro sec
 * @retval None
 */
void usb_udelay(const uint32_t usec)
{
    __IO uint32_t i;
    uint32_t j;

    j = (HCLK_VALUE + 1000000UL - 1UL) / 1000000UL * usec;
    for (i = 0UL; i < j; i++) {
    }
}

/**
 * @brief  This function provides delay time in milli sec
 * @param  [in] msec        Value of delay required in milli sec
 * @retval None
 */
void usb_mdelay(const uint32_t msec)
{
    usb_udelay(msec * 1000UL);
}

/**
 * @}
 */

/*******************************************************************************
 * EOF (not truncated)
 ******************************************************************************/
