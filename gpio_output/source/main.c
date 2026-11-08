/**
 *******************************************************************************
 * @file  gpio/gpio_output/source/main.c
 * @brief Main program of GPIO for the Device Driver Library.
 @verbatim
   Change Logs:
   Date             Author          Notes
   2022-03-31       CDT             First version
 @endverbatim
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

/**
 * @addtogroup HC32F460_DDL_Examples
 * @{
 */

/**
 * @addtogroup GPIO_OUTPUT
 * @{
 */

/*******************************************************************************
 * Local type definitions ('typedef')
 ******************************************************************************/

/*******************************************************************************
 * Local pre-processor symbols/macros ('#define')
 ******************************************************************************/
/* LED_R Port/Pin definition */
#define LED_R_PORT          (GPIO_PORT_C)
#define LED_R_PIN           (GPIO_PIN_09)

#if 0
/* LED_G Port/Pin definition */
#define LED_G_PORT          (GPIO_PORT_D)
#define LED_G_PIN           (GPIO_PIN_04)
/* LED_Y Port/Pin definition */
#define LED_Y_PORT          (GPIO_PORT_D)
#define LED_Y_PIN           (GPIO_PIN_05)
/* LED_B Port/Pin definition */
#define LED_B_PORT          (GPIO_PORT_D)
#define LED_B_PIN           (GPIO_PIN_06)
#endif

/* LED toggle definition */
#define LED_R_TOGGLE()      (GPIO_TogglePins(LED_R_PORT, LED_R_PIN))
#if 0
#define LED_G_TOGGLE()      (GPIO_TogglePins(LED_G_PORT, LED_G_PIN))
#define LED_Y_TOGGLE()      (GPIO_TogglePins(LED_Y_PORT, LED_Y_PIN))
#define LED_B_TOGGLE()      (GPIO_TogglePins(LED_B_PORT, LED_B_PIN))
#endif
#define DLY_MS              (1000UL)

/* XTAL port/pin definition (PH0: XTAL_IN, PH1: XTAL_OUT) */
#define XTAL_PORT           (GPIO_PORT_H)
#define XTAL_PIN            (GPIO_PIN_00 | GPIO_PIN_01)

/*******************************************************************************
 * Global variable definitions (declared in header file with 'extern')
 ******************************************************************************/

/*******************************************************************************
 * Local function prototypes ('static')
 ******************************************************************************/
static void SystemClockInit(void);

/*******************************************************************************
 * Local variable definitions ('static')
 ******************************************************************************/

/*******************************************************************************
 * Function implementation - global ('extern') and local ('static')
 ******************************************************************************/
/**
 * @brief  System clock initialize.
 *         XTAL 16MHz -> MPLL 200MHz:
 *         (16MHz / PLLM(2) * PLLN(50)) / PLLP(2) = 200MHz, VCO = 400MHz
 * @param  None
 * @retval None
 */
static void SystemClockInit(void)
{
    stc_clock_xtal_init_t stcXtalInit;
    stc_clock_pll_init_t  stcMpllInit;

    /* Set bus clock divider. HCLK = 200MHz, PCLK1 = 100MHz ... */
    CLK_SetClockDiv(CLK_BUS_CLK_ALL, (CLK_HCLK_DIV1 | CLK_EXCLK_DIV2 | CLK_PCLK0_DIV1 | \
                                      CLK_PCLK1_DIV2 | CLK_PCLK2_DIV4 | CLK_PCLK3_DIV4 | CLK_PCLK4_DIV2));

    /* XTAL pin as analog function */
    GPIO_AnalogCmd(XTAL_PORT, XTAL_PIN, ENABLE);
    (void)CLK_XtalStructInit(&stcXtalInit);
    stcXtalInit.u8State      = CLK_XTAL_ON;
    stcXtalInit.u8Mode       = CLK_XTAL_MD_OSC;
    stcXtalInit.u8Drv        = CLK_XTAL_DRV_MID;   /* 16MHz: [16, 20)MHz -> Mid */
    stcXtalInit.u8StableTime = CLK_XTAL_STB_2MS;
    (void)CLK_XtalInit(&stcXtalInit);

    /* MPLL config: 16MHz / 2 * 50 / 2 = 200MHz */
    (void)CLK_PLLStructInit(&stcMpllInit);
    stcMpllInit.PLLCFGR       = 0UL;
    stcMpllInit.PLLCFGR_f.PLLM  = (2UL - 1UL);
    stcMpllInit.PLLCFGR_f.PLLN  = (50UL - 1UL);
    stcMpllInit.PLLCFGR_f.PLLP  = (2UL - 1UL);
    stcMpllInit.PLLCFGR_f.PLLQ  = (2UL - 1UL);
    stcMpllInit.PLLCFGR_f.PLLR  = (2UL - 1UL);
    stcMpllInit.PLLCFGR_f.PLLSRC = CLK_PLL_SRC_XTAL;
    stcMpllInit.u8PLLState    = CLK_PLL_ON;
    (void)CLK_PLLInit(&stcMpllInit);

    /* Wait MPLL ready. */
    while (SET != CLK_GetStableStatus(CLK_STB_FLAG_PLL)) {
        ;
    }

    /* SRAM wait cycle setting */
    SRAM_SetWaitCycle(SRAM_SRAMH, SRAM_WAIT_CYCLE0, SRAM_WAIT_CYCLE0);
    SRAM_SetWaitCycle((SRAM_SRAM12 | SRAM_SRAM3 | SRAM_SRAMR), SRAM_WAIT_CYCLE1, SRAM_WAIT_CYCLE1);

    /* Flash read wait cycle setting (126MHz ~ 200MHz: 5 wait cycles) */
    (void)EFM_SetWaitCycle(EFM_WAIT_CYCLE5);
    /* GPIO read wait cycle: 3 cycles for 126MHz ~ 200MHz */
    GPIO_SetReadWaitCycle(GPIO_RD_WAIT3);
    /* Switch driver ability */
    (void)PWC_HighSpeedToHighPerformance();
    /* Switch system clock source to MPLL. */
    CLK_SetSysClockSrc(CLK_SYSCLK_SRC_PLL);

    /* Reset cache ram and enable cache */
    EFM_CacheRamReset(ENABLE);
    EFM_CacheRamReset(DISABLE);
    EFM_CacheCmd(ENABLE);
}

/**
 * @brief  LED Init
 * @param  None
 * @retval None
 */
static void LED_Init(void)
{
    stc_gpio_init_t stcGpioInit;

    (void)GPIO_StructInit(&stcGpioInit);
    stcGpioInit.u16PinState = PIN_STAT_RST;
    stcGpioInit.u16PinDir = PIN_DIR_OUT;
    (void)GPIO_Init(LED_R_PORT, LED_R_PIN, &stcGpioInit);
	#if 0
    (void)GPIO_Init(LED_G_PORT, LED_G_PIN, &stcGpioInit);
    (void)GPIO_Init(LED_Y_PORT, LED_Y_PIN, &stcGpioInit);
    (void)GPIO_Init(LED_B_PORT, LED_B_PIN, &stcGpioInit);
	#endif
}

/**
 * @brief  Main function of GPIO project
 * @param  None
 * @retval int32_t return value, if needed
 */
int32_t main(void)
{
    /* Register write enable for some required peripherals. */
    LL_PERIPH_WE(LL_PERIPH_EFM | LL_PERIPH_FCG | LL_PERIPH_GPIO | LL_PERIPH_PWC_CLK_RMU | LL_PERIPH_SRAM);
    /* System clock: XTAL 16MHz -> MPLL 200MHz */
    SystemClockInit();
    /* LED initialize */
    LED_Init();
    /* Register write protected for some required peripherals. */
    LL_PERIPH_WP(LL_PERIPH_EFM | LL_PERIPH_GPIO | LL_PERIPH_PWC_CLK_RMU | LL_PERIPH_SRAM);
    for (;;) {
        LED_R_TOGGLE();
        DDL_DelayMS(DLY_MS);
			#if 0
        LED_G_TOGGLE();
        DDL_DelayMS(DLY_MS);
        LED_Y_TOGGLE();
        DDL_DelayMS(DLY_MS);
        LED_B_TOGGLE();
        DDL_DelayMS(DLY_MS);
			#endif
        /* De-init port if necessary */
        // GPIO_DeInit();
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
