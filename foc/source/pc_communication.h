/**
 *******************************************************************************
 * @file  foc/source/pc_communication.h
 * @brief PC host tool communication protocol - public API.
 *
 * Re-implementation of the STM32F446 DRV8301 firmware's PC link
 * (original: user/pc_communication/inc/communication.h, closed source .lib)
 * on top of this project's XHSC USB CDC device stack.
 *
 * The wire protocol is unchanged, see the original project's PC protocol doc:
 *   - down  PC -> MCU : 55 55 <cmd> [payload], no length, no CRC, big-endian
 *   - up    MCU -> PC : 55 55 02 <12B UID> AA AA          (ID reply, 17 B)
 *                       55 55 04 <1715 payload> AA AA    (wave frame, 1715 B)
 *   - multi-byte numbers are big-endian, float is IEEE-754 binary32.
 *
 * Timing (this project, HC32F460):
 *   - communication_task()      : sampling tick, call from the 20 kHz PWM ISR
 *   - communication_handle()    : command parse, call at 100 Hz (1 ms soft divide)
 *   - communication_send_task() : frame flush, call from the main loop
 *     (sending from the main loop avoids sharing the CDC IN ring with the USB
 *      interrupt while the USART3 bridge is a second producer)
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
#ifndef __PC_COMMUNICATION_H__
#define __PC_COMMUNICATION_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

/*******************************************************************************
 * Protocol geometry (fixed by the host tool, do not change)
 ******************************************************************************/
#define PC_FLOAT_WAVE_NUM       (6U)        /* wave upload channels, float      */
#define PC_INT_WAVE_NUM         (4U)        /* wave upload channels, int32      */
#define PC_FLOAT_DATA_NUM       (10U)       /* float scalar upload channels     */
#define PC_INT_DATA_NUM         (10U)       /* int32 scalar upload channels     */
#define PC_FLOAT_DL_NUM         (10U)       /* float download parameters        */

#define PC_SAMPLE_POINTS        (40U)       /* samples per wave block           */
#define PC_WAVE_BLOCK_LEN       (161U)      /* 1 tag + 40 * 4 bytes             */
#define PC_FRAME_LEN            (1715U)     /* 0x6B3, full waveform frame       */
#define PC_ID_FRAME_LEN         (17U)       /* ID reply frame                   */

/* Sample tick base: communication_task() is called once per PWM/ADC tick.
 * The host asks for a per-channel sample rate f; the divisor is
 * PC_SAMPLE_BASE_HZ / f. Keep this equal to the ISR call rate. */
#define PC_SAMPLE_BASE_HZ       (20000U)

/*******************************************************************************
 * Global functions
 ******************************************************************************/
/* Reset the registration tables and the protocol state. */
extern void communication_init(void);

/* Sample one tick worth of data. Call from the PWM ISR at PC_SAMPLE_BASE_HZ. */
extern void communication_task(void);

/* Parse a pending host command. Call at 100 Hz. */
extern void communication_handle(void);

/* Flush a finished waveform frame to the CDC IN endpoint. Call from the main
 * loop; returns immediately when nothing is ready. */
extern void communication_send_task(void);

/* Registration API (same names and index ranges as the original firmware).
 * Out-of-range indices are silently ignored, like the original library.
 *   float_wave_upload_init : index 0 .. 5
 *   int_wave_upload_init   : index 0 .. 3
 *   float/int_data_upload_init : index 0 .. 9
 *   float_data_download_init   : index 0 .. 9  (host index is 1-based)
 */
extern void float_wave_upload_init(uint8_t index, float *data);
extern void int_wave_upload_init(uint8_t index, uint8_t type, int32_t *data);
extern void float_data_upload_init(uint8_t index, float *data);
extern void int_data_upload_init(uint8_t index, uint8_t type, int32_t *data);
extern void float_data_download_init(uint8_t index, float *data);
extern void speed_ref_data_download_init(float *data);
extern void motor_start_stop_data_download_init(uint8_t *data);

/* USB CDC RX hook: called from vcp_rxdata() inside the USB interrupt.
 * Stores one host packet for communication_handle() to parse. */
extern void pc_communication_rx(const uint8_t *buf, uint32_t len);

/*******************************************************************************
 * Exported state (debug / telemetry only)
 ******************************************************************************/
extern volatile uint8_t  g_pcUploadFlag;     /* 1: streaming enabled          */
extern volatile uint32_t g_pcSampleFactor;   /* PC_SAMPLE_BASE_HZ / freq      */
extern volatile uint32_t g_pcFrameDropCnt;   /* frames dropped (CDC ring busy)*/

#ifdef __cplusplus
}
#endif

#endif /* __PC_COMMUNICATION_H__ */

/*******************************************************************************
 * EOF (not truncated)
 ******************************************************************************/
