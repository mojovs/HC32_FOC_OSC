/**
 *******************************************************************************
 * @file  foc/source/cdc_data_process.c
 * @brief CDC <-> USART3 bridge + local RX ring buffer for the foc firmware.
 *
 * Changes against the vendor example:
 *   - USART3 pins: PB6 = TX (Func_Grp2, GPIO_FUNC_32), PB7 = RX (GPIO_FUNC_33)
 *     instead of the EV board's PH2/PC13.
 *   - 115200-8-N-1 by default, USART_CLK_DIV64 (PCLK1 = 100 MHz), and
 *     USART_HW_FLOWCTRL_NONE because CN1 has no RTS/CTS.
 *   - The USB -> UART direction no longer blocks inside the USB interrupt:
 *     vcp_rxdata() only pushes into a ring buffer, vcp_task() drains it from
 *     the main loop.
 *
 * foc 改动说明 (由 usb_dev_cdc + usart_uart_int 合并而来):
 *   - USART3 RX 数据有两个去处, 互不影响:
 *       1) USB CDC IN 端点 (例如: PC 把命令发给 CN1 时回显给 PC);
 *       2) g_stcUartRing 接收环形缓冲 (取自 usart_uart_int, 供主循环
 *          通过 UART_ReadByte 逐字节读取);
 *   - VCP_LOCAL_ECHO 置 0: foc 把 CDC 当作上位机/PC 链路, 因此关闭
 *     PC 侧的本地回显 (usb_dev_cdc 用于串口终端调试时才需要)。
 *
 * Ported from the XHSC DDL example usb_dev_cdc.
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
#include "cdc_data_process.h"
#include "pc_communication.h"

/**
 * @addtogroup SMotorDrive_USB_Dev_Cdc
 * @{
 */

/*******************************************************************************
 * Local pre-processor symbols/macros ('#define')
 ******************************************************************************/
#define CDC_COMM                        (CM_USART3)
#define USART_FCG_ENABLE()              (FCG_Fcg1PeriphClockCmd(FCG1_PERIPH_USART3, ENABLE))

/* UART RX/TX port/pin definition - CN1: 2 = RX (PB7), 3 = TX (PB6) */
#define USART_RX_PORT                   (GPIO_PORT_B)
#define USART_RX_PIN                    (GPIO_PIN_07)
#define USART_RX_GPIO_FUNC              (GPIO_FUNC_33)

#define USART_TX_PORT                   (GPIO_PORT_B)
#define USART_TX_PIN                    (GPIO_PIN_06)
#define USART_TX_GPIO_FUNC              (GPIO_FUNC_32)

/* UART unit interrupt definition */
#define USART_UNIT_ERR_INT              (INT_SRC_USART3_EI)
#define USART_UNIT_ERR_IRQn             (INT000_IRQn)

#define USART_UNIT_RX_INT               (INT_SRC_USART3_RI)
#define USART_UNIT_RX_IRQn              (INT001_IRQn)

#define VCP_DEFAULT_BAUDRATE            (115200UL)

/* CDC -> UART3 staging buffer (power of two). Written by the USB interrupt,
   read by vcp_task() in the main loop. */
#define VCP_TX_RING_SIZE                (1024U)
#define VCP_TX_RING_MASK                (VCP_TX_RING_SIZE - 1U)
#define VCP_TX_BURST                    (16U)       /* bytes per vcp_task() call */
#define VCP_TX_SPIN_MAX                 (4000UL)    /* TX_EMPTY poll limit */

/*
 * Bring-up aid.
 *   1 = everything the PC sends over USB is mirrored straight back to the PC
 *       (in addition to being forwarded to USART3). The terminal then shows
 *       exactly what you type, which proves both USB directions work without
 *       any jumper on CN1.
 *   0 = pure bridge (PC -> USART3 only), the vendor behaviour.
 * Set to 0 when the CDC port is used as a real console for the motor drive,
 * otherwise every command you type is echoed back on the screen.
 *
 * foc: CDC 用作上位机/PC 链路, 因此置 0。
 */
#define VCP_LOCAL_ECHO                  (0U)

/*******************************************************************************
 * Local variable definitions ('static')
 ******************************************************************************/
static LINE_CODING linecoding = {
    VCP_DEFAULT_BAUDRATE, /* baud rate */
    0x00,                 /* stop bits - 1 */
    0x00,                 /* parity    - none */
    0x08                  /* data bit  - 8 */
};

static uint8_t m_au8TxRing[VCP_TX_RING_SIZE];
static volatile uint16_t m_u16TxHead = 0U;      /* producer: USB ISR */
static volatile uint16_t m_u16TxTail = 0U;      /* consumer: vcp_task() */

/*
 * 本工程自用的 RX 环形缓冲 (取自 usart_uart_int 的 500 字节缓冲):
 * USART3 RX 中断写入, 由 UART_ReadByte() 读取, 与发往 PC 的通道独立。
 */
static uint8_t m_au8UartRing[UART_RING_SIZE];
stc_ring_buf_t g_stcUartRing;

/*
 * Text queued when the PC terminal opens the port.
 *
 * The greeting itself is queued by the main loop on the SET_CONFIGURATION
 * event, i.e. before the terminal is open. Many terminal programs purge the
 * driver's RX buffer when they open the port, which swallows that greeting.
 * This second message is queued on SET_CONTROL_LINE_STATE (DTR, i.e. "port
 * opened") so it always reaches the screen. If the terminal never raises DTR,
 * the first byte typed by the user triggers it instead.
 */
#if (VCP_BRIDGE_TO_UART != 0U)
static const uint8_t m_au8PortOpened[] =
    "[host opened the port]\r\n"
    "SMotorDrive VCP: PC -> board commands, CN1 traffic -> PC.\r\n";
#endif
/* Cleared on every SET_CONFIGURATION. */
static uint8_t m_u8PortOpenedMsg = 0U;

/*******************************************************************************
 * Local function prototypes ('static')
 ******************************************************************************/
static void UsartErrIrqCallback(void);
static void UsartRxIrqCallback(void);
static void InstalIrqHandler(const stc_irq_signin_config_t *pstcConfig,
                             uint32_t u32Priority);
static uint16_t VcpUartInit(uint32_t u32Baudrate, uint32_t u32ClockDiv,
                            uint32_t u32DataWidth, uint32_t u32Parity, uint32_t u32StopBit);
static uint16_t VcpApplyLineCoding(void);
static void VcpQueuePortOpened(void);

/*******************************************************************************
 * Function implementation - global ('extern') and local ('static')
 ******************************************************************************/
/**
 * @brief  Configure USART3 from scratch (pins + FCG + frame + baudrate).
 *         The USART must not be transmitting/receiving while this runs.
 * @param  [in] u32Baudrate  baudrate
 * @param  [in] u32ClockDiv  USART_CLK_DIV1/4/16/64
 * @param  [in] u32DataWidth USART_DATA_WIDTH_8BIT/9BIT
 * @param  [in] u32Parity    USART_PARITY_NONE/EVEN/ODD
 * @param  [in] u32StopBit   USART_STOPBIT_1BIT/2BIT
 * @retval USB_DEV_OK / USB_DEV_FAIL
 */
static uint16_t VcpUartInit(uint32_t u32Baudrate, uint32_t u32ClockDiv,
                            uint32_t u32DataWidth, uint32_t u32Parity, uint32_t u32StopBit)
{
    stc_usart_uart_init_t stcUartInit;

    (void)USART_UART_StructInit(&stcUartInit);
    stcUartInit.u32ClockSrc         = USART_CLK_SRC_INTERNCLK;
    stcUartInit.u32ClockDiv         = u32ClockDiv;
    stcUartInit.u32CKOutput         = USART_CK_OUTPUT_DISABLE;
    stcUartInit.u32Baudrate         = u32Baudrate;
    stcUartInit.u32DataWidth        = u32DataWidth;
    stcUartInit.u32StopBit          = u32StopBit;
    stcUartInit.u32Parity           = u32Parity;
    stcUartInit.u32OverSampleBit    = USART_OVER_SAMPLE_8BIT;
    stcUartInit.u32FirstBit         = USART_FIRST_BIT_LSB;
    stcUartInit.u32StartBitPolarity = USART_START_BIT_FALLING;
    /* Neither RTS nor CTS is routed on CN1. USART_UART_Init() only programs
       CR3.CTSE for the CTS value, so RTS here == no hardware flow control.
       (The DDL assertion rejects 0UL.) */
    stcUartInit.u32HWFlowControl    = USART_HW_FLOWCTRL_RTS;

    if (LL_OK != USART_UART_Init(CDC_COMM, &stcUartInit, NULL)) {
        return USB_DEV_FAIL;
    }

    return USB_DEV_OK;
}

/**
 * @brief  Initializes the configuration of usart port
 * @param  None
 * @retval None
 */
void vcp_init(void)
{
    stc_irq_signin_config_t stcIrqSigninCfg;

    m_u8PortOpenedMsg = 0U;

    /* 初始化接收环形缓冲 (取自 usart_uart_int 的做法) */
    (void)BUF_Init(&g_stcUartRing, m_au8UartRing, sizeof(m_au8UartRing));

    /* Configure USART RX/TX pin. */
    GPIO_SetFunc(USART_RX_PORT, USART_RX_PIN, USART_RX_GPIO_FUNC);
    GPIO_SetFunc(USART_TX_PORT, USART_TX_PIN, USART_TX_GPIO_FUNC);

    /* Enable peripheral clock */
    USART_FCG_ENABLE();

    /* 115200-8-N-1, PCLK1 = 100 MHz -> DIV64 */
    if (USB_DEV_OK != VcpUartInit(linecoding.bitrate, USART_CLK_DIV64,
                                  USART_DATA_WIDTH_8BIT, USART_PARITY_NONE, USART_STOPBIT_1BIT)) {
        return;
    }

    /* Register error IRQ handler && configure NVIC. */
    stcIrqSigninCfg.enIRQn     = USART_UNIT_ERR_IRQn;
    stcIrqSigninCfg.enIntSrc   = USART_UNIT_ERR_INT;
    stcIrqSigninCfg.pfnCallback = &UsartErrIrqCallback;
    InstalIrqHandler(&stcIrqSigninCfg, DDL_IRQ_PRIO_DEFAULT);

    /* Register RX IRQ handler && configure NVIC. */
    stcIrqSigninCfg.enIRQn     = USART_UNIT_RX_IRQn;
    stcIrqSigninCfg.enIntSrc   = USART_UNIT_RX_INT;
    stcIrqSigninCfg.pfnCallback = &UsartRxIrqCallback;
    InstalIrqHandler(&stcIrqSigninCfg, DDL_IRQ_PRIO_DEFAULT);

    USART_FuncCmd(CDC_COMM, (USART_RX | USART_TX), ENABLE);
    USART_FuncCmd(CDC_COMM, USART_INT_RX, ENABLE);
}

/**
 * @brief  deInitializes the Media
 * @param  None
 * @retval None
 */
void vcp_deinit(void)
{
    USART_FuncCmd(CDC_COMM, USART_INT_RX, DISABLE);
    USART_FuncCmd(CDC_COMM, (USART_RX | USART_TX), DISABLE);
}

/**
 * @brief  Queue the "port opened" text. Called for every
 *         SET_CONTROL_LINE_STATE from the host (safe from the USB interrupt:
 *         memory copy only). It is purposely not de-duplicated, because the
 *         host may toggle DTR=0/1 while opening the port and the useful one
 *         must not be thrown away.
 * @param  None
 * @retval None
 */
static void VcpQueuePortOpened(void)
{
    m_u8PortOpenedMsg = 1U;
#if (VCP_BRIDGE_TO_UART != 0U)
    vcp_send((uint8_t *)m_au8PortOpened, (uint32_t)(sizeof(m_au8PortOpened) - 1U));
#endif
}

/**
 * @brief  Manage the CDC class requests
 * @param  [in] Cmd     Command code
 * @param  [in] Buf     data to be sent or received
 * @param  [in] Len     data length in bytes
 * @retval status
 */
void vcp_ctrlpare(uint32_t Cmd, uint8_t *Buf, uint32_t Len)
{
    switch (Cmd) {
        case SEND_ENCAPSULATED_COMMAND:
            /* Not  needed for this driver */
            break;

        case GET_ENCAPSULATED_RESPONSE:
            /* Not  needed for this driver */
            break;

        case SET_COMM_FEATURE:
            /* Not  needed for this driver */
            break;

        case GET_COMM_FEATURE:
            /* Not  needed for this driver */
            break;

        case CLEAR_COMM_FEATURE:
            /* Not  needed for this driver */
            break;

        case SET_LINE_CODING:
            if ((Buf != NULL) && (Len >= 7UL)) {
                linecoding.bitrate    = ((uint32_t)Buf[0U] | ((uint32_t)Buf[1U] << 8U) |
                                         ((uint32_t)Buf[2U] << 16U) | ((uint32_t)Buf[3U] << 24U));
                linecoding.format     = Buf[4U];
                linecoding.paritytype = Buf[5U];
                linecoding.datatype   = Buf[6U];
                /* Re-arm the physical UART with the host's settings. */
                (void)VcpApplyLineCoding();
            }
            break;
        case GET_LINE_CODING:
            Buf[0U] = (uint8_t)(linecoding.bitrate);
            Buf[1U] = (uint8_t)(linecoding.bitrate >> 8U);
            Buf[2U] = (uint8_t)(linecoding.bitrate >> 16U);
            Buf[3U] = (uint8_t)(linecoding.bitrate >> 24U);
            Buf[4U] = linecoding.format;
            Buf[5U] = linecoding.paritytype;
            Buf[6U] = linecoding.datatype;
            break;
        case SET_CONTROL_LINE_STATE:
            /*
             * DTR/RTS from the host: CN1 has no modem lines, but this is the
             * moment the PC terminal opens the port. Queue the text here so it
             * survives terminals that purge the RX buffer on open.
             */
            VcpQueuePortOpened();
            break;
        case SEND_BREAK:
            /* Not  needed for this driver */
            break;
        default:
            break;
    }
}

/**
 * @brief  Apply the current line coding to USART3. Baudrate is tried from the
 *         slowest USART clock to the fastest one, so 115200 and higher both
 *         work with the 100 MHz PCLK1.
 * @param  None
 * @retval USB_DEV_OK / USB_DEV_FAIL
 */
static uint16_t VcpApplyLineCoding(void)
{
    static const uint32_t au32Div[] = {
        USART_CLK_DIV64, USART_CLK_DIV16, USART_CLK_DIV4, USART_CLK_DIV1
    };
    uint32_t u32DataWidth;
    uint32_t u32Parity;
    uint32_t u32StopBit;
    uint32_t i;

    /* Only 7/8 data bits and the 3 standard parities are supported. */
    if (linecoding.datatype == 7U) {
        /* 7 data bits are sent as 8 bits + parity */
        u32DataWidth = USART_DATA_WIDTH_8BIT;
    } else if (linecoding.datatype == 8U) {
        u32DataWidth = (linecoding.paritytype == 0U) ? USART_DATA_WIDTH_8BIT : USART_DATA_WIDTH_9BIT;
    } else {
        return USB_DEV_FAIL;
    }

    switch (linecoding.paritytype) {
        case 0U:
            u32Parity = USART_PARITY_NONE;
            break;
        case 1U:
            u32Parity = USART_PARITY_EVEN;
            break;
        case 2U:
            u32Parity = USART_PARITY_ODD;
            break;
        default:
            return USB_DEV_FAIL;
    }

    switch (linecoding.format) {
        case 0U:
        case 1U:
            u32StopBit = USART_STOPBIT_1BIT;
            break;
        case 2U:
            u32StopBit = USART_STOPBIT_2BIT;
            break;
        default:
            return USB_DEV_FAIL;
    }

    if ((linecoding.bitrate == 0UL) || (linecoding.bitrate > 3000000UL)) {
        return USB_DEV_FAIL;
    }

    /* TX/RX must be off before PR/BRR are rewritten (DDL assertion). */
    USART_FuncCmd(CDC_COMM, (USART_RX | USART_TX), DISABLE);

    for (i = 0UL; i < (sizeof(au32Div) / sizeof(au32Div[0])); i++) {
        if (USB_DEV_OK == VcpUartInit(linecoding.bitrate, au32Div[i],
                                      u32DataWidth, u32Parity, u32StopBit)) {
            USART_FuncCmd(CDC_COMM, (USART_RX | USART_TX | USART_INT_RX), ENABLE);
            return USB_DEV_OK;
        }
    }

    /* Nothing worked: restore 115200-8-N-1. */
    linecoding.bitrate    = VCP_DEFAULT_BAUDRATE;
    linecoding.format     = 0x00U;
    linecoding.paritytype = 0x00U;
    linecoding.datatype   = 0x08U;
    (void)VcpUartInit(linecoding.bitrate, USART_CLK_DIV64,
                      USART_DATA_WIDTH_8BIT, USART_PARITY_NONE, USART_STOPBIT_1BIT);
    USART_FuncCmd(CDC_COMM, (USART_RX | USART_TX | USART_INT_RX), ENABLE);

    return USB_DEV_FAIL;
}

/**
 * @brief  Receive data from USART and save in the buffer uart_rx_buffer, the
 *         received number is counted by APP_Rx_ptr_in
 *         (called from the USART3 RX interrupt)
 *
 *         foc 说明: 收到的字节写入本工程自用环形缓冲 (usart_uart_int 做法),
 *         由 UART_ReadByte() 在主循环里读取。
 * @param  None
 * @retval None
 */
void vcp_txdata(void)
{
    uint8_t u8Data;

    if (linecoding.datatype == 7U) {
        u8Data = (uint8_t)USART_ReadData(CDC_COMM) & 0x7FU;
    } else {
        u8Data = (uint8_t)USART_ReadData(CDC_COMM);
    }

    uart_rx_buffer[APP_Rx_ptr_in] = u8Data;
    APP_Rx_ptr_in++;

    /* To avoid buffer overflow */
    if (APP_Rx_ptr_in == APP_RX_DATA_SIZE) {
        APP_Rx_ptr_in = 0U;
    }

    /* 同时写入接收环形缓冲: CN1 的数据可在主循环里取用 (usart_uart_int 的做法) */
    (void)BUF_Write(&g_stcUartRing, &u8Data, 1UL);
}

/**
 * @brief  Queue data for the CDC IN endpoint (board -> PC), bypassing USART3.
 *         Safe to call from the main loop while the RX interrupt appends to the
 *         same buffer: the slot is written before the 32-bit pointer is bumped,
 *         and both accesses are single-word (no torn pointer).
 * @param  [in] Buf     data to send to the host
 * @param  [in] Len     data length in bytes
 * @retval None
 */
void vcp_send(uint8_t *Buf, uint32_t Len)
{
    uint32_t i;

    for (i = 0UL; i < Len; i++) {
        uart_rx_buffer[APP_Rx_ptr_in] = Buf[i];
        APP_Rx_ptr_in++;
        if (APP_Rx_ptr_in == APP_RX_DATA_SIZE) {
            APP_Rx_ptr_in = 0U;
        }
    }
}

/**
 * @brief  Data received from USB is queued for USART3 TX.
 *         Called from the USB interrupt: no waiting, drop on overflow.
 * @param  [in] Buf     buffer to be sent to uart port
 * @param  [in] Len     data length in bytes
 * @retval None
 */
void vcp_rxdata(uint8_t *Buf, uint32_t Len)
{
    uint32_t i;
    uint16_t u16Next;

    /*
     * Last safety net: if the host never raised DTR, the first packet it
     * actually sends proves the user is looking at the terminal, so queue the
     * text then.
     */
    if (0U == m_u8PortOpenedMsg) {
        VcpQueuePortOpened();
    }

    /* Feed the PC host protocol engine (parsed later by communication_handle). */
    pc_communication_rx(Buf, Len);

#if (VCP_LOCAL_ECHO != 0U)
    /* Mirror the PC's bytes back so the terminal shows what was typed. */
    vcp_send(Buf, Len);
#endif

#if (VCP_BRIDGE_TO_UART != 0U)
    for (i = 0UL; i < Len; i++) {
        u16Next = (uint16_t)((m_u16TxHead + 1U) & VCP_TX_RING_MASK);
        if (u16Next == m_u16TxTail) {
            break;              /* ring full: drop the rest of the packet */
        }
        m_au8TxRing[m_u16TxHead] = Buf[i];
        m_u16TxHead = u16Next;
    }
#else
    (void)i;
    (void)u16Next;
#endif
}

/**
 * @brief  Push queued USB data to USART3. Call from the main loop.
 *         A short bounded burst keeps the physical UART at line rate without
 *         blocking the USB interrupt.
 * @param  None
 * @retval None
 */
void vcp_task(void)
{
    uint16_t u16Cnt;
    uint32_t u32Spin;

    for (u16Cnt = 0U; u16Cnt < VCP_TX_BURST; u16Cnt++) {
        if (m_u16TxTail == m_u16TxHead) {
            break;
        }

        u32Spin = 0UL;
        while (SET != USART_GetStatus(CDC_COMM, USART_FLAG_TX_EMPTY)) {
            if (++u32Spin >= VCP_TX_SPIN_MAX) {
                return;         /* transmitter busy, try again next call */
            }
        }

        USART_WriteData(CDC_COMM, (uint16_t)m_au8TxRing[m_u16TxTail]);
        m_u16TxTail = (uint16_t)((m_u16TxTail + 1U) & VCP_TX_RING_MASK);
    }
}

/**
 * @brief  USART RX irq callback function.
 * @param  None
 * @retval None
 */
void UsartRxIrqCallback(void)
{
    if (SET == USART_GetStatus(CDC_COMM, USART_FLAG_RX_FULL)) {
        /* 把收到的数据发给 PC 上位机 (桥接方向) */
        vcp_txdata();
    }
}

/**
 * @brief  USART RX error irq callback function.
 * @param  None
 * @retval None
 */
void UsartErrIrqCallback(void)
{
    if (SET == USART_GetStatus(CDC_COMM, USART_FLAG_FRAME_ERR)) {
        USART_ClearStatus(CDC_COMM, USART_FLAG_FRAME_ERR);
    }

    if (SET == USART_GetStatus(CDC_COMM, USART_FLAG_PARITY_ERR)) {
        USART_ClearStatus(CDC_COMM, USART_FLAG_PARITY_ERR);
    }

    if (SET == USART_GetStatus(CDC_COMM, USART_FLAG_OVERRUN)) {
        USART_ClearStatus(CDC_COMM, USART_FLAG_OVERRUN);
    }

    /* Drain whatever is still in the receive register. */
    (void)USART_ReadData(CDC_COMM);
}

/**
 * @brief  从接收环形缓冲取走 1 个字节 (取自 usart_uart_int 的
 *         UART_ReadByte 实现), 供 CN1 反向上行到 USB 的路径使用。
 *         返回 1 表示取到 1 个字节, 0 表示缓冲为空。
 * @param  [out] pu8Data  读出的字节
 * @retval 1: 读到 1 个字节; 0: 缓冲为空
 */
uint32_t UART_ReadByte(uint8_t *pu8Data)
{
    return BUF_Read(&g_stcUartRing, pu8Data, 1UL);
}

/**
 * @brief  Instal IRQ handler.
 * @param  [in] pstcConfig      Pointer to struct @ref stc_irq_signin_config_t
 * @param  [in] u32Priority     Interrupt priority
 * @retval None
 */
static void InstalIrqHandler(const stc_irq_signin_config_t *pstcConfig,
                             uint32_t u32Priority)
{
    if (NULL != pstcConfig) {
        (void)INTC_IrqSignIn(pstcConfig);
        NVIC_ClearPendingIRQ(pstcConfig->enIRQn);
        NVIC_SetPriority(pstcConfig->enIRQn, u32Priority);
        NVIC_EnableIRQ(pstcConfig->enIRQn);
    }
}

/**
 * @}
 */

/*******************************************************************************
 * EOF (not truncated)
 ******************************************************************************/
