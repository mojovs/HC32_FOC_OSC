/**
 *******************************************************************************
 * @file  usb_dev_cdc/source/main.c
 * @brief USB CDC (virtual COM port) bring-up test for the SMotorDrive board.
 *
 * Board specifics (see docs/hardware/hc32f460keta-pinout.md and the netlist):
 *   - XTAL: 16 MHz (X3), not 8 MHz -> MPLL 200 MHz, PCLK1 100 MHz
 *   - USB FS: DP = PA12, DM = PA11 (USB-C, 5.1 k CC pull-downs)
 *   - No VBUS sense pin: PA9 = PWM_BH. VBUS sensing is compiled out
 *     (usb_app_conf.h) and the core is forced VBUS-valid in usb_bsp.c.
 *   - Console: USART3 PB6 (TX) / PB7 (RX), 115200-8-N-1, on CN1
 *   - LED: PC9, active low
 *
 * Behaviour: the CDC "COM port" is bridged to USART3 (both directions),
 * exactly like the vendor example, so a loopback on CN1 (TX<->RX) or an
 * external USB-TTL adapter can be used to verify the link.
 *
 * Note: this is a bring-up test, not the FOC firmware. Merging it into
 * drv8301_test means calling vcp_task() from the main loop, keeping the USB
 * and USART3 interrupts below the control-loop priority.
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
#include "main.h"
#include "cdc_data_process.h"
#include <stdio.h>

/**
 * @addtogroup SMotorDrive_USB_Dev_Cdc
 * @{
 */

/*******************************************************************************
 * Local pre-processor symbols/macros ('#define')
 ******************************************************************************/
/* Peripheral register WE/WP selection.
   NOTE: LL_PERIPH_FCG must be unlocked for CLK_SetSysClockSrc(PLL), otherwise
   PWC_FCG0PC.PRT0 stays write protected and the DDL assertion fires. */
#define LL_PERIPH_SEL                   (LL_PERIPH_GPIO | LL_PERIPH_FCG | LL_PERIPH_PWC_CLK_RMU | \
                                         LL_PERIPH_EFM | LL_PERIPH_SRAM)
/* GPIO stays unlocked: the heartbeat toggles PC9 through GPIO_TogglePins()
   from the main loop. Write-protecting GPIO would freeze the LED. */
#define LL_PERIPH_WP_SEL                (LL_PERIPH_EFM | LL_PERIPH_PWC_CLK_RMU | LL_PERIPH_SRAM)

#define XTAL_PORT                       (GPIO_PORT_H)
#define XTAL_PIN                        (GPIO_PIN_00 | GPIO_PIN_01)

#define LED_PORT                        (GPIO_PORT_C)
#define LED_PIN                         (GPIO_PIN_09)       /* active low */
#define LED_BLINK_MS                    (500UL)

/*
 * USB FS clock (PLLXP) must be exactly 48 MHz.
 *   16 MHz / 4 = 4 MHz (PLLX input, 1..25 MHz)
 *   4 MHz * 84 = 336 MHz (VCO, 240..480 MHz)
 *   336 MHz / 7 = 48 MHz
 * PLLX is independent of the main PLL, so the 200 MHz / PCLK1 100 MHz
 * configuration is untouched.
 */
#define USB_PLL_M                       (4UL)
#define USB_PLL_N                       (84UL)
#define USB_PLL_P                       (7UL)

/*******************************************************************************
 * Global variable definitions (declared in header file with 'extern')
 ******************************************************************************/
usb_core_instance usb_dev;

/*******************************************************************************
 * Local function prototypes ('static')
 ******************************************************************************/
static void SystemClockInit(void);
static void LedInit(void);
static void LedTask(void);
static void UsbEventTask(void);

/*******************************************************************************
 * Function implementation - global ('extern') and local ('static')
 ******************************************************************************/
/**
 * @brief  System clock: 16 MHz XTAL -> MPLL 200 MHz (PCLK1 100 MHz),
 *         PLLX -> 48 MHz for the USB FS core.
 */
static void SystemClockInit(void)
{
    stc_clock_xtal_init_t stcXtalInit;
    stc_clock_pll_init_t  stcMpllInit;
    stc_clock_pllx_init_t stcUpllInit;
    uint32_t u32Wait;

    /* HCLK = 200 MHz, PCLK1 = 100 MHz (USART3 divider assumes this). */
    CLK_SetClockDiv(CLK_BUS_CLK_ALL, (CLK_HCLK_DIV1 | CLK_EXCLK_DIV2 | CLK_PCLK0_DIV1 | \
                                      CLK_PCLK1_DIV2 | CLK_PCLK2_DIV4 | CLK_PCLK3_DIV4 | CLK_PCLK4_DIV2));

    GPIO_AnalogCmd(XTAL_PORT, XTAL_PIN, ENABLE);
    (void)CLK_XtalStructInit(&stcXtalInit);
    stcXtalInit.u8State      = CLK_XTAL_ON;
    stcXtalInit.u8Mode       = CLK_XTAL_MD_OSC;
    stcXtalInit.u8Drv        = CLK_XTAL_DRV_MID;
    stcXtalInit.u8StableTime = CLK_XTAL_STB_2MS;
    (void)CLK_XtalInit(&stcXtalInit);

    u32Wait = 100UL;
    while ((SET != CLK_GetStableStatus(CLK_STB_FLAG_XTAL)) && (u32Wait > 0UL)) {
        DDL_DelayMS(1UL);
        u32Wait--;
    }
    if (SET != CLK_GetStableStatus(CLK_STB_FLAG_XTAL)) {
        for (;;) {           /* 16 MHz crystal did not start */
        }
    }

    /* MPLL: 16 MHz / 2 * 50 / 2 = 200 MHz */
    (void)CLK_PLLStructInit(&stcMpllInit);
    stcMpllInit.PLLCFGR          = 0UL;
    stcMpllInit.PLLCFGR_f.PLLM   = (2UL - 1UL);
    stcMpllInit.PLLCFGR_f.PLLN   = (50UL - 1UL);
    stcMpllInit.PLLCFGR_f.PLLP   = (2UL - 1UL);
    stcMpllInit.PLLCFGR_f.PLLQ   = (2UL - 1UL);
    stcMpllInit.PLLCFGR_f.PLLR   = (2UL - 1UL);
    stcMpllInit.PLLCFGR_f.PLLSRC = CLK_PLL_SRC_XTAL;
    stcMpllInit.u8PLLState       = CLK_PLL_ON;
    (void)CLK_PLLInit(&stcMpllInit);

    u32Wait = 100UL;
    while ((SET != CLK_GetStableStatus(CLK_STB_FLAG_PLL)) && (u32Wait > 0UL)) {
        DDL_DelayMS(1UL);
        u32Wait--;
    }
    if (SET != CLK_GetStableStatus(CLK_STB_FLAG_PLL)) {
        for (;;) {           /* MPLL did not lock */
        }
    }

    /* PLLX (UPLL) for USB FS: 16 MHz / 4 * 84 / 7 = 48 MHz */
    (void)CLK_PLLxStructInit(&stcUpllInit);
    stcUpllInit.PLLCFGR        = 0UL;
    stcUpllInit.PLLCFGR_f.PLLM = (USB_PLL_M - 1UL);
    stcUpllInit.PLLCFGR_f.PLLN = (USB_PLL_N - 1UL);
    stcUpllInit.PLLCFGR_f.PLLP = (USB_PLL_P - 1UL);
    stcUpllInit.PLLCFGR_f.PLLQ = (USB_PLL_P - 1UL);
    stcUpllInit.PLLCFGR_f.PLLR = (USB_PLL_P - 1UL);
    stcUpllInit.u8PLLState     = CLK_PLLX_ON;
    (void)CLK_PLLxInit(&stcUpllInit);

    u32Wait = 100UL;
    while ((SET != CLK_GetStableStatus(CLK_STB_FLAG_PLLX)) && (u32Wait > 0UL)) {
        DDL_DelayMS(1UL);
        u32Wait--;
    }
    if (SET != CLK_GetStableStatus(CLK_STB_FLAG_PLLX)) {
        for (;;) {           /* PLLX did not lock, USB will not enumerate */
        }
    }

    SRAM_SetWaitCycle(SRAM_SRAMH, SRAM_WAIT_CYCLE0, SRAM_WAIT_CYCLE0);
    SRAM_SetWaitCycle((SRAM_SRAM12 | SRAM_SRAM3 | SRAM_SRAMR), SRAM_WAIT_CYCLE1, SRAM_WAIT_CYCLE1);
    (void)EFM_SetWaitCycle(EFM_WAIT_CYCLE5);
    GPIO_SetReadWaitCycle(GPIO_RD_WAIT3);
    (void)PWC_HighSpeedToHighPerformance();
    CLK_SetSysClockSrc(CLK_SYSCLK_SRC_PLL);

    EFM_CacheRamReset(ENABLE);
    EFM_CacheRamReset(DISABLE);
    EFM_CacheCmd(ENABLE);
}

/**
 * @brief  LED (PC9) is active low.
 */
static void LedInit(void)
{
    stc_gpio_init_t stcGpioInit;

    (void)GPIO_StructInit(&stcGpioInit);
    stcGpioInit.u16PinDir        = PIN_DIR_OUT;
    stcGpioInit.u16PinOutputType = PIN_OUT_TYPE_CMOS;
    stcGpioInit.u16PinDrv        = PIN_LOW_DRV;
    stcGpioInit.u16PinState      = PIN_SET;          /* off */
    stcGpioInit.u16PinAttr       = PIN_ATTR_DIGITAL;
    (void)GPIO_Init(LED_PORT, LED_PIN, &stcGpioInit);
}

/**
 * @brief  Heartbeat: proves the CPU is alive even without USB attached.
 *         Called once per millisecond from the main loop.
 */
static void LedTask(void)
{
    static uint32_t m_u32Ms = 0UL;

    m_u32Ms++;
    if (m_u32Ms >= LED_BLINK_MS) {
        m_u32Ms = 0UL;
        GPIO_TogglePins(LED_PORT, LED_PIN);
    }
}

/**
 * @brief  Print USB stack events queued by the user callbacks.
 *         The callbacks run in the USB interrupt, so they only set a flag;
 *         printf() stays in the main loop.
 */
static void UsbEventTask(void)
{
    static uint8_t m_au8Greeting[] =
        "\r\nSMotorDrive VCP ready.\r\n"
        "Echo path: CN1-2 <-> CN1-3 loopback, 115200-8-N-1.\r\n";
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
            /* Greeting straight over the CDC IN endpoint, independent of CN1. */
            vcp_send(m_au8Greeting, (uint32_t)(sizeof(m_au8Greeting) - 1U));
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
 * @brief  main function
 * @param  None
 * @retval None
 */
int32_t main(void)
{
    stc_usb_port_identify stcPortIdentify;

    LL_PERIPH_WE(LL_PERIPH_SEL);

    SystemClockInit();
    LedInit();

    /* usb_dev_init() -> usb_bsp_init() (pins, USB clock, console)
                        -> core/FIFO/interrupts -> user_init() */
    stcPortIdentify.u8CoreID = USBFS_CORE_ID;
    usb_dev_init(&usb_dev, &stcPortIdentify, &user_desc, &class_cdc_cbk, &user_cb);

    LL_PERIPH_WP(LL_PERIPH_WP_SEL);

    printf("\r\n========================================\r\n");
    printf(" SMotorDrive USB CDC test\r\n");
    printf(" HC32F460KETA, 16 MHz XTAL, 200 MHz\r\n");
    printf(" USB: PA11(DM)/PA12(DP), CDC <-> USART3\r\n");
    printf(" CN1: 2=PB7(RX), 3=PB6(TX), 115200-8-N-1\r\n");
    printf("========================================\r\n");

    for (;;) {
        vcp_task();          /* CDC(OUT) -> USART3 TX, non-blocking */
        UsbEventTask();
        LedTask();
        DDL_DelayMS(1UL);
    }
}

/**
 * @}
 */

/*******************************************************************************
 * EOF (not truncated)
 ******************************************************************************/
