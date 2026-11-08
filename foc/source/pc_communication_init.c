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
 * Local type definitions ('typedef')
 ******************************************************************************/

/*******************************************************************************
 * Local pre-processor symbols/macros ('#define')
 ******************************************************************************/
/* Firmware-generated sine test frequency, in Hz. It must stay well below half
 * of PC_SAMPLE_BASE_HZ (the sample tick rate) for the host to resolve it. */
#define SINE_TEST_HZ        (100.0f)

/*******************************************************************************
 * Local variable definitions ('static')
 ******************************************************************************/
/* 256-entry full-wave LUT, sin(2*pi*i/256). A lookup table is used instead of
 * sinf() because both MDK targets link the MicroLib, which has no sinf(). */
static const float s_afSineLut[256] = {
    0.00000000f, 0.02454123f, 0.04906767f, 0.07356456f,
    0.09801714f, 0.12241068f, 0.14673047f, 0.17096189f,
    0.19509032f, 0.21910124f, 0.24298018f, 0.26671276f,
    0.29028468f, 0.31368174f, 0.33688985f, 0.35989504f,
    0.38268343f, 0.40524131f, 0.42755509f, 0.44961133f,
    0.47139674f, 0.49289819f, 0.51410274f, 0.53499762f,
    0.55557023f, 0.57580819f, 0.59569930f, 0.61523159f,
    0.63439328f, 0.65317284f, 0.67155895f, 0.68954054f,
    0.70710678f, 0.72424708f, 0.74095113f, 0.75720885f,
    0.77301045f, 0.78834643f, 0.80320753f, 0.81758481f,
    0.83146961f, 0.84485357f, 0.85772861f, 0.87008699f,
    0.88192126f, 0.89322430f, 0.90398929f, 0.91420976f,
    0.92387953f, 0.93299280f, 0.94154407f, 0.94952818f,
    0.95694034f, 0.96377607f, 0.97003125f, 0.97570213f,
    0.98078528f, 0.98527764f, 0.98917651f, 0.99247953f,
    0.99518473f, 0.99729046f, 0.99879546f, 0.99969882f,
    0.99999999f, 0.99969882f, 0.99879546f, 0.99729046f,
    0.99518473f, 0.99247953f, 0.98917651f, 0.98527764f,
    0.98078528f, 0.97570213f, 0.97003125f, 0.96377607f,
    0.95694034f, 0.94952818f, 0.94154407f, 0.93299280f,
    0.92387953f, 0.91420976f, 0.90398929f, 0.89322430f,
    0.88192126f, 0.87008699f, 0.85772861f, 0.84485357f,
    0.83146961f, 0.81758481f, 0.80320753f, 0.78834643f,
    0.77301045f, 0.75720885f, 0.74095113f, 0.72424708f,
    0.70710678f, 0.68954054f, 0.67155895f, 0.65317284f,
    0.63439328f, 0.61523159f, 0.59569930f, 0.57580819f,
    0.55557023f, 0.53499762f, 0.51410274f, 0.49289819f,
    0.47139674f, 0.44961133f, 0.42755509f, 0.40524131f,
    0.38268343f, 0.35989504f, 0.33688985f, 0.31368174f,
    0.29028468f, 0.26671276f, 0.24298018f, 0.21910124f,
    0.19509032f, 0.17096189f, 0.14673047f, 0.12241068f,
    0.09801714f, 0.07356456f, 0.04906767f, 0.02454123f,
   -0.00000000f, -0.02454123f, -0.04906767f, -0.07356456f,
   -0.09801714f, -0.12241068f, -0.14673047f, -0.17096189f,
   -0.19509032f, -0.21910124f, -0.24298018f, -0.26671276f,
   -0.29028468f, -0.31368174f, -0.33688985f, -0.35989504f,
   -0.38268343f, -0.40524131f, -0.42755509f, -0.44961133f,
   -0.47139674f, -0.49289819f, -0.51410274f, -0.53499762f,
   -0.55557023f, -0.57580819f, -0.59569930f, -0.61523159f,
   -0.63439328f, -0.65317284f, -0.67155895f, -0.68954054f,
   -0.70710678f, -0.72424708f, -0.74095113f, -0.75720885f,
   -0.77301045f, -0.78834643f, -0.80320753f, -0.81758481f,
   -0.83146961f, -0.84485357f, -0.85772861f, -0.87008699f,
   -0.88192126f, -0.89322430f, -0.90398929f, -0.91420976f,
   -0.92387953f, -0.93299280f, -0.94154407f, -0.94952818f,
   -0.95694034f, -0.96377607f, -0.97003125f, -0.97570213f,
   -0.98078528f, -0.98527764f, -0.98917651f, -0.99247953f,
   -0.99518473f, -0.99729046f, -0.99879546f, -0.99969882f,
   -0.99999999f, -0.99969882f, -0.99879546f, -0.99729046f,
   -0.99518473f, -0.99247953f, -0.98917651f, -0.98527764f,
   -0.98078528f, -0.97570213f, -0.97003125f, -0.96377607f,
   -0.95694034f, -0.94952818f, -0.94154407f, -0.93299280f,
   -0.92387953f, -0.91420976f, -0.90398929f, -0.89322430f,
   -0.88192126f, -0.87008699f, -0.85772861f, -0.84485357f,
   -0.83146961f, -0.81758481f, -0.80320753f, -0.78834643f,
   -0.77301045f, -0.75720885f, -0.74095113f, -0.72424708f,
   -0.70710678f, -0.68954054f, -0.67155895f, -0.65317284f,
   -0.63439328f, -0.61523159f, -0.59569930f, -0.57580819f,
   -0.55557023f, -0.53499762f, -0.51410274f, -0.49289819f,
   -0.47139674f, -0.44961133f, -0.42755509f, -0.40524131f,
   -0.38268343f, -0.35989504f, -0.33688985f, -0.31368174f,
   -0.29028468f, -0.26671276f, -0.24298018f, -0.21910124f,
   -0.19509032f, -0.17096189f, -0.14673047f, -0.12241068f,
   -0.09801714f, -0.07356456f, -0.04906767f, -0.02454123f,
};

/*******************************************************************************
 * Global variable definitions
 ******************************************************************************/
float   g_pcSineTest = 0.0f;   /* float wave channel 0, see pc_test_sine_task */

/* 32-bit phase accumulator and its per-tick step (fraction of one full turn). */
static uint32_t s_u32SinePhase = 0U;
static uint32_t s_u32SineStep  = 0U;

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
 * @brief  Advance the sine test signal by one sample tick.
 *
 * Call from the same ADC/DMA ISR as communication_task(), before it, so the
 * freshly computed sample is what the current tick captures.
 */
void pc_test_sine_task(void)
{
    s_u32SinePhase += s_u32SineStep;
    g_pcSineTest = s_afSineLut[s_u32SinePhase >> 24U];
}

/**
 * @brief  Reset the engine and register every protocol channel.
 */
void pc_communication_init(void)
{
    uint32_t i;

    communication_init();

    /* Per-tick phase step = f_sine / f_sample, as a 32-bit fraction of a turn. */
    s_u32SineStep = (uint32_t)(((double)SINE_TEST_HZ / (double)PC_SAMPLE_BASE_HZ)
                               * 4294967296.0);
    s_u32SinePhase = 0U;

    /* 6 float wave channels (frame blocks 0..5). Channel 0 carries the
     * firmware-generated sine test signal; 1..5 stay on placeholder values
     * until the real FOC variables are ported. */
    float_wave_upload_init(0U, &g_pcSineTest);
    for (i = 1UL; i < PC_FLOAT_WAVE_NUM; i++) {
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
