/**
 *******************************************************************************
 * @file  usart/usart_uart_int/source/main.c
 * @brief This example demonstrates UART RX by interrupt and blocking printf TX.
 @verbatim
   Change Logs:
   Date             Author          Notes
   2022-03-31       CDT             First version
   2022-10-31       CDT             Read USART_DR.RDR when USART overrun error occur
   2026-10-19       SMotorDrive     Port to HC32F460KETA FOC board:
                                    - USART3 on PB6 (TX) / PB7 (RX)
                                    - XTAL 16MHz -> MPLL 200MHz, PCLK1 = 100MHz
                                    - RX interrupt + ring buffer
                                    - printf retargeted to USART3
                                    - LED/heartbeat diagnostics removed
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
#include <stdio.h>
#include "main.h"

/**
 * @addtogroup HC32F460_DDL_Examples
 * @{
 */

/**
 * @addtogroup USART_UART_Interrupt
 * @{
 */

/*******************************************************************************
 * Local type definitions ('typedef')
 ******************************************************************************/

/*******************************************************************************
 * Local pre-processor symbols/macros ('#define')
 ******************************************************************************/
/* Peripheral register WE/WP selection */
#define LL_PERIPH_SEL                   (LL_PERIPH_GPIO | LL_PERIPH_FCG | LL_PERIPH_PWC_CLK_RMU | \
                                         LL_PERIPH_EFM | LL_PERIPH_SRAM)

/* USART RX/TX pin definition */
#define USART_RX_PORT                   (GPIO_PORT_B)   /* PB7: USART3_RX, Func_Grp2 */
#define USART_RX_PIN                    (GPIO_PIN_07)
#define USART_RX_GPIO_FUNC              (GPIO_FUNC_33)

#define USART_TX_PORT                   (GPIO_PORT_B)   /* PB6: USART3_TX, Func_Grp2 */
#define USART_TX_PIN                    (GPIO_PIN_06)
#define USART_TX_GPIO_FUNC              (GPIO_FUNC_32)

/* USART unit definition */
#define USART_UNIT                      (CM_USART3)
#define USART_FCG_ENABLE()              (FCG_Fcg1PeriphClockCmd(FCG1_PERIPH_USART3, ENABLE))

/* USART interrupt definition */
#define USART_RX_ERR_IRQn               (INT000_IRQn)
#define USART_RX_ERR_INT_SRC            (INT_SRC_USART3_EI)

#define USART_RX_FULL_IRQn              (INT001_IRQn)
#define USART_RX_FULL_INT_SRC           (INT_SRC_USART3_RI)

/* XTAL port/pin definition (PH0: XTAL_IN, PH1: XTAL_OUT) */
#define XTAL_PORT                       (GPIO_PORT_H)
#define XTAL_PIN                        (GPIO_PIN_00 | GPIO_PIN_01)

/* Ring buffer size */
#define RING_BUF_SIZE                   (500UL)

/*******************************************************************************
 * Global variable definitions (declared in header file with 'extern')
 ******************************************************************************/

/*******************************************************************************
 * Local function prototypes ('static')
 ******************************************************************************/
static void SystemClockInit(void);
static void UART_Init(void);
static void UART_SendByte(uint8_t u8Data);
static uint32_t UART_ReadByte(uint8_t *pu8Data);

/*******************************************************************************
 * Local variable definitions ('static')
 ******************************************************************************/
static uint8_t m_au8DataBuf[RING_BUF_SIZE];
static stc_ring_buf_t m_stcRingBuf;

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
    uint32_t u32Wait;

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

    /* Wait XTAL ready (max 100ms). */
    u32Wait = 100UL;
    while ((SET != CLK_GetStableStatus(CLK_STB_FLAG_XTAL)) && (u32Wait > 0UL)) {
        DDL_DelayMS(1UL);
        u32Wait--;
    }
    if (SET != CLK_GetStableStatus(CLK_STB_FLAG_XTAL)) {
        /* XTAL did not lock: stop here. Replace with your own error handler. */
        for (;;) {
        }
    }

    /* MPLL config: 16MHz / 2 * 50 / 2 = 200MHz */
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

    /* Wait MPLL ready (max 100ms). */
    u32Wait = 100UL;
    while ((SET != CLK_GetStableStatus(CLK_STB_FLAG_PLL)) && (u32Wait > 0UL)) {
        DDL_DelayMS(1UL);
        u32Wait--;
    }
    if (SET != CLK_GetStableStatus(CLK_STB_FLAG_PLL)) {
        /* MPLL did not lock: stop here. Replace with your own error handler. */
        for (;;) {
        }
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
 * @brief  Send one byte by polling (blocking).
 * @param  [in] u8Data          Byte to send
 * @retval None
 */
static void UART_SendByte(uint8_t u8Data)
{
    while (SET != USART_GetStatus(USART_UNIT, USART_FLAG_TX_EMPTY)) {
        ;
    }
    USART_WriteData(USART_UNIT, (uint16_t)u8Data);
}

/**
 * @brief  Read one received byte from the ring buffer (non-blocking).
 * @param  [out] pu8Data        Pointer to store the received byte
 * @retval 1: a byte was read; 0: no data
 */
static uint32_t UART_ReadByte(uint8_t *pu8Data)
{
    return BUF_Read(&m_stcRingBuf, pu8Data, 1UL);
}

/**
 * @brief  Retarget C library printf to USART3 (blocking).
 *         Keil MDK MicroLIB calls fputc() for every character printed.
 * @param  [in] ch              Character to send
 * @param  [in] f               FILE pointer (not used)
 * @retval The character written
 */
int fputc(int ch, FILE *f)
{
    (void)f;

    UART_SendByte((uint8_t)ch);

    return ch;
}

/**
 * @brief  USART RX IRQ callback
 * @param  None
 * @retval None
 */
static void USART_RxFull_IrqCallback(void)
{
    uint8_t u8Data = (uint8_t)USART_ReadData(USART_UNIT);

    (void)BUF_Write(&m_stcRingBuf, &u8Data, 1UL);
}

/**
 * @brief  USART error IRQ callback.
 * @param  None
 * @retval None
 */
static void USART_RxError_IrqCallback(void)
{
    (void)USART_ReadData(USART_UNIT);

    USART_ClearStatus(USART_UNIT, (USART_FLAG_PARITY_ERR | USART_FLAG_FRAME_ERR | USART_FLAG_OVERRUN));
}

/**
 * @brief  Instal IRQ handler.
 * @param  [in] pstcConfig      Pointer to struct @ref stc_irq_signin_config_t
 * @param  [in] u32Priority     Interrupt priority
 * @retval None
 */
static void INTC_IrqInstalHandler(const stc_irq_signin_config_t *pstcConfig, uint32_t u32Priority)
{
    if (NULL != pstcConfig) {
        (void)INTC_IrqSignIn(pstcConfig);
        NVIC_ClearPendingIRQ(pstcConfig->enIRQn);
        NVIC_SetPriority(pstcConfig->enIRQn, u32Priority);
        NVIC_EnableIRQ(pstcConfig->enIRQn);
    }
}

/**
 * @brief  UART initialize: pins, peripheral clock, baud rate and RX interrupt.
 *         This is the part to copy when porting UART to another project.
 * @param  None
 * @retval None
 */
static void UART_Init(void)
{
    stc_usart_uart_init_t stcUartInit;
    stc_irq_signin_config_t stcIrqSigninConfig;

    /* Configure USART RX/TX pin. */
    GPIO_SetFunc(USART_RX_PORT, USART_RX_PIN, USART_RX_GPIO_FUNC);
    GPIO_SetFunc(USART_TX_PORT, USART_TX_PIN, USART_TX_GPIO_FUNC);

    /* Enable peripheral clock. */
    USART_FCG_ENABLE();

    /* Initialize RX ring buffer. */
    (void)BUF_Init(&m_stcRingBuf, m_au8DataBuf, sizeof(m_au8DataBuf));

    /* Initialize UART. PCLK1 = 100MHz, DIV64 -> 1.5625MHz USART clock. */
    (void)USART_UART_StructInit(&stcUartInit);
    stcUartInit.u32ClockDiv = USART_CLK_DIV64;
    stcUartInit.u32Baudrate = 115200UL;
    stcUartInit.u32OverSampleBit = USART_OVER_SAMPLE_8BIT;
    if (LL_OK != USART_UART_Init(USART_UNIT, &stcUartInit, NULL)) {
        for (;;) {
        }
    }

    /* Register RX error IRQ handler && configure NVIC. */
    stcIrqSigninConfig.enIRQn = USART_RX_ERR_IRQn;
    stcIrqSigninConfig.enIntSrc = USART_RX_ERR_INT_SRC;
    stcIrqSigninConfig.pfnCallback = &USART_RxError_IrqCallback;
    INTC_IrqInstalHandler(&stcIrqSigninConfig, DDL_IRQ_PRIO_DEFAULT);

    /* Register RX full IRQ handler && configure NVIC. */
    stcIrqSigninConfig.enIRQn = USART_RX_FULL_IRQn;
    stcIrqSigninConfig.enIntSrc = USART_RX_FULL_INT_SRC;
    stcIrqSigninConfig.pfnCallback = &USART_RxFull_IrqCallback;
    INTC_IrqInstalHandler(&stcIrqSigninConfig, DDL_IRQ_PRIO_DEFAULT);

    /* Enable TX, RX and RX interrupt. Keep TX enabled so the line idles high. */
    USART_FuncCmd(USART_UNIT, (USART_TX | USART_RX | USART_INT_RX), ENABLE);
}

/**
 * @brief  Main function of UART interrupt project
 * @param  None
 * @retval int32_t return value, if needed
 */
int32_t main(void)
{
    uint8_t u8Data;

    /* MCU Peripheral registers write unprotected.
       NOTE: LL_PERIPH_FCG must be kept here. PWC_FCG0PC.PRT0 is 0 after reset
       (FCG0 write protected), and CLK_SetSysClockSrc(PLL) asserts PRT0 == 1.
       Without unlocking FCG0 the Debug build hangs forever in
       DDL_AssertHandler() (hc32_ll_clk.c:576). */
    LL_PERIPH_WE(LL_PERIPH_SEL);

    /* Board clock: 16MHz XTAL -> MPLL 200MHz. */
    SystemClockInit();

    /* UART pins, baud rate and RX interrupt. */
    UART_Init();

    /* MCU Peripheral registers write protected.
       NOTE: LL_PERIPH_FCG is intentionally not protected, see the note above. */
    LL_PERIPH_WP(LL_PERIPH_EFM | LL_PERIPH_GPIO | LL_PERIPH_PWC_CLK_RMU | LL_PERIPH_SRAM);

    for (;;) {
        /* Host -> MCU: RX interrupt fills the ring buffer.
           Application demo only: '?' help, '1' status, other bytes echo.
           Delete this switch if you only need a raw byte echo. */
        while (0UL != UART_ReadByte(&u8Data)) {
            switch (u8Data) {
                case '?':
                    printf("\r\nCommands: '?' = help, '1' = status\r\n");
                    break;
                case '1':
                    printf("\r\nStatus: USART3 OK, 115200-8-N-1\r\n");
                    break;
                default:
                    UART_SendByte(u8Data);
                    break;
            }
        }
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
