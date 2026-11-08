/**
 *******************************************************************************
 * @file  foc/source/pc_communication_init.h
 * @brief Channel registration for the PC host tool protocol.
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
#ifndef __PC_COMMUNICATION_INIT_H__
#define __PC_COMMUNICATION_INIT_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

/* Placeholder upload channels (test values). Replace with the real FOC
 * variables once motor/foc_algorithm.c is ported. Exposed for debugging. */
extern int32_t g_pcIntTest[10];
extern float   g_pcFloatTest[10];

/* Writable placeholders for the host download channels:
 *   cmd 0x06 -> g_pcFloatDownload[0..9] (wire index 1..10)
 *   cmd 0x08/0x09 -> g_pcSpeedRef (+-5 Hz)
 *   cmd 0x07/0x0A -> g_pcMotorStartStop */
extern float   g_pcFloatDownload[10];
extern float   g_pcSpeedRef;
extern uint8_t g_pcMotorStartStop;

/* Register every channel and reset the protocol engine. Call once at startup,
 * before USB enumeration is complete. */
extern void pc_communication_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __PC_COMMUNICATION_INIT_H__ */

/*******************************************************************************
 * EOF (not truncated)
 ******************************************************************************/
