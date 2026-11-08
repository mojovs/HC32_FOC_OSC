/**
 *******************************************************************************
 * @file  foc/source/pc_communication_init.c
 * @brief Register the PC host tool channels.
 *
 * Mirrors the original project's user/pc_communication_init.c. The original
 * registered real FOC variables; this project does not have them yet, so the
 * slots are filled with test values. Swap the registrations for the real
 * variables when motor/foc_algorithm.c lands:
 *
 *   float wave 0..5 : Theta, EKF[0], EKF[1], EKF[2], Ibeta, Iq_ref
 *   int   wave 0..3 : ia_test, int_test2, int_test3, int_test4
 *   float scalar    : L_RF[0..2], Ibeta, Ialpha, Q_3_3, Q_0_0 ...
 *   float download   : SPEED_PI_P, SPEED_PI_I, R_0_0, R_1_1, Q_x_x, flux
 *   speed_ref        : Speed_Ref (Hz)
 *   motor_start_stop : motor_start_stop (u8)
 *
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
#include "pc_communication.h"
#include "pc_communication_init.h"

/*******************************************************************************
 * Global variable definitions
 ******************************************************************************/
int32_t g_pcIntTest[10]   = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
float   g_pcFloatTest[10] = { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f,
                              6.0f, 7.0f, 8.0f, 9.0f, 0.0f };

float   g_pcFloatDownload[10];      /* cmd 0x06 targets                   */
float   g_pcSpeedRef       = 0.0f;  /* cmd 0x08 / 0x09, unit Hz           */
uint8_t g_pcMotorStartStop = 0U;    /* cmd 0x07 / 0x0A                    */

/*******************************************************************************
 * Function implementation
 ******************************************************************************/
/**
 * @brief  Reset the engine and register every protocol channel.
 */
void pc_communication_init(void)
{
    uint32_t i;

    communication_init();

    /* 6 float wave channels (frame blocks 0..5). */
    for (i = 0UL; i < PC_FLOAT_WAVE_NUM; i++) {
        float_wave_upload_init((uint8_t)i, &g_pcFloatTest[i]);
    }

    /* 4 int32 wave channels (frame blocks 6..9), type tag 0x06. */
    for (i = 0UL; i < PC_INT_WAVE_NUM; i++) {
        int_wave_upload_init((uint8_t)i, 0x06U, &g_pcIntTest[i]);
    }

    /* 10 float scalar channels. */
    for (i = 0UL; i < PC_FLOAT_DATA_NUM; i++) {
        float_data_upload_init((uint8_t)i, &g_pcFloatTest[i]);
    }

    /* 10 int32 scalar channels. */
    for (i = 0UL; i < PC_INT_DATA_NUM; i++) {
        int_data_upload_init((uint8_t)i, 0x06U, &g_pcIntTest[i]);
    }

    /* 10 float download parameters (host index 1..10). */
    for (i = 0UL; i < PC_FLOAT_DL_NUM; i++) {
        float_data_download_init((uint8_t)i, &g_pcFloatDownload[i]);
    }

    /* Motor control commands. */
    speed_ref_data_download_init(&g_pcSpeedRef);
    motor_start_stop_data_download_init(&g_pcMotorStartStop);
}

/*******************************************************************************
 * EOF (not truncated)
 ******************************************************************************/
