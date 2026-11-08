/**
 *******************************************************************************
 * @file  foc/source/cdc_data_process.h
 * @brief Header file for cdc_data_process.c
 *
 * Ported from the XHSC DDL example usb_dev_cdc. USART3 now lives on PB6/PB7
 * (CN1) and the CDC -> UART direction is buffered instead of blocking inside
 * the USB interrupt.
 *
 * foc �ϲ�˵��: �� usb_dev_cdc ���Žӹ���֮��, ������ usart_uart_int ��
 * ���ؽ��ջ����� (UART_ReadByte, ������), �������������ʹ�á�
 @verbatim
   Change Logs:
   Date             Author          Notes
   2022-03-31       CDT             First version
   2026-10-20       SMotorDrive     PB6/PB7, 115200 8N1, non-blocking TX
   2026-10-26       SMotorDrive     �ϲ��� foc ����: + ���� RX ������
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
#ifndef __CDC_DATA_PROCESS_H__
#define __CDC_DATA_PROCESS_H__

/* C binding of definitions if building with C++ compiler */
#ifdef __cplusplus
extern "C"
{
#endif

/*******************************************************************************
 * Include files
 ******************************************************************************/
#include "usb_dev_cdc_class.h"
#include "ring_buf.h"

/**
 * @addtogroup SMotorDrive_USB_Dev_Cdc
 * @{
 */

/*******************************************************************************
 * Global type definitions ('typedef')
 ******************************************************************************/
/* all needed parameters to be configured for the ComPort.
 * These parameters can modified on the fly by the host through CDC class
 * command class requests. */
typedef struct {
    uint32_t bitrate;
    uint8_t  format;
    uint8_t  paritytype;
    uint8_t  datatype;
} LINE_CODING;

/*******************************************************************************
 * Global pre-processor symbols/macros ('#define')
 ******************************************************************************/
#define DEFAULT_CONFIG              (0U)
#define OTHER_CONFIG                (1U)

#define UART_RING_SIZE              (500U)

/*
 * USB <-> USART3 bridge switch (0/1):
 *   1 = bytes the PC sends over USB are also forwarded to CN1, and the boot /
 *       port-opened greeting text is pushed to the PC.
 *   0 = USB feeds only the PC host protocol engine (pc_communication); the text
 *       greeting to the PC is suppressed so it cannot disturb frame parsing.
 * The foc project uses the CDC port as the host link, so this defaults to 0.
 */
#define VCP_BRIDGE_TO_UART          (0U)

/*******************************************************************************
  Global function prototypes (definition in C source)
 ******************************************************************************/
extern void vcp_init(void);
extern void vcp_deinit(void);
extern void vcp_ctrlpare(uint32_t Cmd, uint8_t *Buf, uint32_t Len);
extern void vcp_txdata(void);
extern void vcp_rxdata(uint8_t *Buf, uint32_t Len);

/*
 * Queue data for the CDC IN endpoint (board -> PC) without touching the
 * physical UART. Used for the "VCP ready" greeting. Not part of the vendor API.
 */
extern void vcp_send(uint8_t *Buf, uint32_t Len);

/*
 * Drain the CDC -> USART3 TX buffer. Must be called from the main loop (or a
 * low priority task). Never call it from an interrupt.
 * Not part of the vendor API.
 */
extern void vcp_task(void);

/*
 * ������������ (���� usart_uart_int): USART3 RX �жϰ�ÿ���ֽ�ͬʱ�ͽ�
 * g_stcUartRing ������, ��ѭ�������������ȡ�ֽ������ؽ�����
 * ���� 1 ��ʾ���� 1 �ֽ�, 0 ��ʾ�����ݡ�
 */
extern uint32_t UART_ReadByte(uint8_t *pu8Data);

/* ���ؽ��ջ�����ʵ�� (RX �ж�д, ��ѭ����)�� */
extern stc_ring_buf_t g_stcUartRing;

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* __CDC_DATA_PROCESS_H__ */

/*******************************************************************************
 * EOF (not truncated)
 ******************************************************************************/
