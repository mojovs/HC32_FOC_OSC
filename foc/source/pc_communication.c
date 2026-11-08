/**
 *******************************************************************************
 * @file  foc/source/pc_communication.c
 * @brief PC host tool communication protocol engine.
 *
 * Re-implementation of the original STM32F446 firmware's closed-source
 * pc_communication library, on the XHSC USB CDC device stack. The frame
 * layout and command set match the host tool bit for bit (big-endian,
 * 55 55 header, AA AA tail, 1715-byte waveform frame).
 *
 * Slot layout of the 1715-byte waveform frame (offset 0 = 0x55):
 *   [0]    55 55 04
 *   [3]     10 wave blocks, 161 B each (3 + 161*k, k = 0..9)
 *             float blocks  k = 0..5   tag 0x01, 40 x f32 at +1+4*s
 *             int32 blocks  k = 6..9   tag = registered type, 40 x i32
 *   [1613]  10 float scalars, 5 B each: tag 0x01 + f32
 *   [1663]  10 int32 scalars, 5 B each: tag + i32
 *   [1713]  AA AA
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
#include "cdc_data_process.h"
#include "usb_dev_cdc_class.h"      /* APP_Rx_ptr_in/out, APP_RX_DATA_SIZE */
#include "hc32_ll.h"                /* EFM_GetUID(), stc_efm_unique_id_t */
#include <string.h>                 /* memcpy() */

/*******************************************************************************
 * Local pre-processor symbols/macros ('#define')
 ******************************************************************************/
#define PC_FRAME_HDR_LEN        (3U)        /* 55 55 04                         */
#define PC_WAVE_BASE            (PC_FRAME_HDR_LEN)
#define PC_FLOAT_DATA_BASE      (1613U)     /* 3 + 10 * 161                     */
#define PC_INT_DATA_BASE        (1663U)     /* 1613 + 10 * 5                    */
#define PC_FRAME_TAIL_OFS       (1713U)     /* PC_FRAME_LEN - 2                 */

#define PC_RX_BUF_SIZE          (64U)       /* one USB FS OUT packet            */

/* Down commands (host -> MCU). */
#define PC_CMD_READ_ID          (0x01U)
#define PC_CMD_SET_RATE         (0x03U)
#define PC_CMD_STOP_STREAM      (0x05U)
#define PC_CMD_WRITE_FLOAT      (0x06U)
#define PC_CMD_MOTOR_START      (0x07U)
#define PC_CMD_SPEED_UP         (0x08U)
#define PC_CMD_SPEED_DOWN       (0x09U)
#define PC_CMD_MOTOR_STOP       (0x0AU)

#define PC_SPEED_STEP_HZ        (5.0f)

/* Up frame types. */
#define PC_UP_TYPE_ID           (0x02U)
#define PC_UP_TYPE_WAVE         (0x04U)

#define PC_TAG_FLOAT            (0x01U)

/*******************************************************************************
 * Local type definitions ('typedef')
 ******************************************************************************/
typedef union {
    float    f;
    uint32_t u;
} pc_float_conv_t;

/*******************************************************************************
 * Local variable definitions ('static')
 ******************************************************************************/
/* Registration tables. NULL means "not registered": the engine writes zero
 * instead of dereferencing (the original library would fault). */
static float   *s_apFloatWave[PC_FLOAT_WAVE_NUM];
static int32_t *s_apIntWave[PC_INT_WAVE_NUM];
static uint8_t  s_au8IntWaveType[PC_INT_WAVE_NUM];
static float   *s_apFloatData[PC_FLOAT_DATA_NUM];
static int32_t *s_apIntData[PC_INT_DATA_NUM];
static uint8_t  s_au8IntDataType[PC_INT_DATA_NUM];
static float   *s_apFloatDl[PC_FLOAT_DL_NUM];
static float   *s_pSpeedRef;
static uint8_t *s_pMotorStartStop;

/* Waveform frame assembly. The PWM ISR fills s_au8Build, then hands the
 * finished frame to s_au8Ready; the main loop flushes s_au8Ready. */
static uint8_t s_au8Build[PC_FRAME_LEN];
static uint8_t s_au8Ready[PC_FRAME_LEN];

/* Streaming state. */
volatile uint8_t  g_pcUploadFlag   = 0U;
volatile uint32_t g_pcSampleFactor = 0U;    /* PC_SAMPLE_BASE_HZ / freq         */
volatile uint32_t g_pcFrameDropCnt = 0UL;

static uint32_t s_u32SampleCnt  = 0UL;      /* divider counter                  */
static uint32_t s_u32GroupCnt   = 0UL;      /* 0 .. 39                          */
static volatile uint8_t s_u8FrameReady = 0U;

/* Host packet, written by the USB ISR, parsed by communication_handle(). */
static volatile uint8_t  s_au8RxBuf[PC_RX_BUF_SIZE];
static volatile uint16_t s_u16RxCount = 0U;

/*******************************************************************************
 * Local function prototypes ('static')
 ******************************************************************************/
static void     PutU16Be(uint8_t *p, uint16_t v);
static void     PutU32Be(uint8_t *p, uint32_t v);
static void     PutF32Be(uint8_t *p, float v);
static uint16_t GetU16Be(const uint8_t *p);
static float    GetF32Be(const uint8_t *p);
static uint8_t  CdcHasRoom(uint32_t len);
static void     SendIdReply(void);

/*******************************************************************************
 * Local helpers
 ******************************************************************************/
static void PutU16Be(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void PutU32Be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void PutF32Be(uint8_t *p, float v)
{
    pc_float_conv_t cv;
    cv.f = v;
    PutU32Be(p, cv.u);
}

static uint16_t GetU16Be(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static float GetF32Be(const uint8_t *p)
{
    pc_float_conv_t cv;
    cv.u = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
    return cv.f;
}

/* Room left in the shared CDC IN ring, with one byte of headroom so that a
 * full/empty ring is never ambiguous. */
static uint8_t CdcHasRoom(uint32_t len)
{
    uint32_t in  = APP_Rx_ptr_in;
    uint32_t out = APP_Rx_ptr_out;
    uint32_t used = (in >= out) ? (in - out)
                                : ((uint32_t)APP_RX_DATA_SIZE - out + in);

    return (((uint32_t)APP_RX_DATA_SIZE - used) > len) ? 1U : 0U;
}

/*******************************************************************************
 * Global functions
 ******************************************************************************/
/**
 * @brief  Reset registration tables and protocol state.
 */
void communication_init(void)
{
    uint32_t i;

    for (i = 0UL; i < PC_FLOAT_WAVE_NUM; i++) {
        s_apFloatWave[i] = NULL;
    }
    for (i = 0UL; i < PC_INT_WAVE_NUM; i++) {
        s_apIntWave[i]     = NULL;
        s_au8IntWaveType[i] = PC_TAG_FLOAT;   /* unused until registered      */
    }
    for (i = 0UL; i < PC_FLOAT_DATA_NUM; i++) {
        s_apFloatData[i] = NULL;
    }
    for (i = 0UL; i < PC_INT_DATA_NUM; i++) {
        s_apIntData[i]     = NULL;
        s_au8IntDataType[i] = PC_TAG_FLOAT;   /* unused until registered      */
    }
    for (i = 0UL; i < PC_FLOAT_DL_NUM; i++) {
        s_apFloatDl[i] = NULL;
    }
    s_pSpeedRef        = NULL;
    s_pMotorStartStop  = NULL;

    g_pcUploadFlag   = 0U;
    g_pcSampleFactor = 0U;
    g_pcFrameDropCnt = 0UL;
    s_u32SampleCnt   = 0UL;
    s_u32GroupCnt    = 0UL;
    s_u8FrameReady   = 0U;
    s_u16RxCount     = 0U;
}

/**
 * @brief  Sample one tick and, when 40 points are collected, finish the frame.
 *         Called from the 20 kHz PWM ISR.
 */
void communication_task(void)
{
    uint32_t i;
    uint32_t base;

    if ((g_pcUploadFlag != 1U) || (g_pcSampleFactor == 0UL)) {
        return;
    }

    if (++s_u32SampleCnt < g_pcSampleFactor) {
        return;
    }
    s_u32SampleCnt = 0UL;

    /* 6 float wave channels (blocks 0..5). */
    for (i = 0UL; i < PC_FLOAT_WAVE_NUM; i++) {
        float val = 0.0f;
        if (s_apFloatWave[i] != NULL) {
            val = *s_apFloatWave[i];
        }
        base = PC_WAVE_BASE + (PC_WAVE_BLOCK_LEN * i);
        PutF32Be(&s_au8Build[base + 1UL + (4UL * s_u32GroupCnt)], val);
    }

    /* 4 int32 wave channels (blocks 6..9). */
    for (i = 0UL; i < PC_INT_WAVE_NUM; i++) {
        int32_t val = 0;
        if (s_apIntWave[i] != NULL) {
            val = *s_apIntWave[i];
        }
        base = PC_WAVE_BASE + (PC_WAVE_BLOCK_LEN * (PC_FLOAT_WAVE_NUM + i));
        PutU32Be(&s_au8Build[base + 1UL + (4UL * s_u32GroupCnt)], (uint32_t)val);
    }

    if (s_u32GroupCnt < (PC_SAMPLE_POINTS - 1UL)) {
        s_u32GroupCnt++;
        return;
    }

    /* Last point of the window: finalise tags, scalars, header and tail. */
    s_u32GroupCnt = 0UL;

    /* Block tags. */
    for (i = 0UL; i < PC_FLOAT_WAVE_NUM; i++) {
        s_au8Build[PC_WAVE_BASE + (PC_WAVE_BLOCK_LEN * i)] = PC_TAG_FLOAT;
    }
    for (i = 0UL; i < PC_INT_WAVE_NUM; i++) {
        s_au8Build[PC_WAVE_BASE + (PC_WAVE_BLOCK_LEN * (PC_FLOAT_WAVE_NUM + i))] =
            s_au8IntWaveType[i];
    }

    /* 10 float scalars, sampled once at the end of the window. */
    for (i = 0UL; i < PC_FLOAT_DATA_NUM; i++) {
        float val = 0.0f;
        if (s_apFloatData[i] != NULL) {
            val = *s_apFloatData[i];
        }
        s_au8Build[PC_FLOAT_DATA_BASE + (5UL * i)] = PC_TAG_FLOAT;
        PutF32Be(&s_au8Build[PC_FLOAT_DATA_BASE + (5UL * i) + 1UL], val);
    }

    /* 10 int32 scalars. */
    for (i = 0UL; i < PC_INT_DATA_NUM; i++) {
        int32_t val = 0;
        if (s_apIntData[i] != NULL) {
            val = *s_apIntData[i];
        }
        s_au8Build[PC_INT_DATA_BASE + (5UL * i)] = s_au8IntDataType[i];
        PutU32Be(&s_au8Build[PC_INT_DATA_BASE + (5UL * i) + 1UL], (uint32_t)val);
    }

    s_au8Build[0] = 0x55U;
    s_au8Build[1] = 0x55U;
    s_au8Build[2] = PC_UP_TYPE_WAVE;
    s_au8Build[PC_FRAME_TAIL_OFS]      = 0xAAU;
    s_au8Build[PC_FRAME_TAIL_OFS + 1U] = 0xAAU;

    /* Hand the frame to the main loop, unless the previous one is still
     * waiting to be flushed (drop, do not block the control ISR). */
    if (s_u8FrameReady == 0U) {
        (void)memcpy(s_au8Ready, s_au8Build, PC_FRAME_LEN);
        s_u8FrameReady = 1U;
    } else {
        g_pcFrameDropCnt++;
    }
}

/**
 * @brief  Flush a finished waveform frame. Called from the main loop.
 */
void communication_send_task(void)
{
    if (s_u8FrameReady == 0U) {
        return;
    }
    if (CdcHasRoom(PC_FRAME_LEN) == 0U) {
        return;                 /* CDC ring still busy, retry next call */
    }

    vcp_send(s_au8Ready, PC_FRAME_LEN);
    s_u8FrameReady = 0U;
}

/**
 * @brief  Build and queue the 17-byte MCU ID reply (55 55 02 <12B UID> AA AA).
 */
static void SendIdReply(void)
{
    uint8_t au8Buf[PC_ID_FRAME_LEN];
    stc_efm_unique_id_t stcUid;

    EFM_GetUID(&stcUid);
    au8Buf[0] = 0x55U;
    au8Buf[1] = 0x55U;
    au8Buf[2] = PC_UP_TYPE_ID;
    PutU32Be(&au8Buf[3],  stcUid.u32UniqueID0);
    PutU32Be(&au8Buf[7],  stcUid.u32UniqueID1);
    PutU32Be(&au8Buf[11], stcUid.u32UniqueID2);
    au8Buf[15] = 0xAAU;
    au8Buf[16] = 0xAAU;

    if (CdcHasRoom(PC_ID_FRAME_LEN) != 0U) {
        vcp_send(au8Buf, PC_ID_FRAME_LEN);
    }
}

/**
 * @brief  Parse one host command. Called at 100 Hz from the 1 ms task.
 */
void communication_handle(void)
{
    uint8_t  au8Pkt[PC_RX_BUF_SIZE];
    uint16_t len = s_u16RxCount;
    uint32_t i;
    uint8_t  cmd;
    uint8_t  idx;

    if (len < PC_FRAME_HDR_LEN) {
        if (len != 0U) {
            s_u16RxCount = 0U;
        }
        return;
    }
    if (len > PC_RX_BUF_SIZE) {
        len = PC_RX_BUF_SIZE;
    }
    /* Snapshot the volatile packet, then free the slot for the next one. */
    for (i = 0UL; i < (uint32_t)len; i++) {
        au8Pkt[i] = s_au8RxBuf[i];
    }
    s_u16RxCount = 0U;

    if ((au8Pkt[0] != 0x55U) || (au8Pkt[1] != 0x55U)) {
        return;
    }
    cmd = au8Pkt[2];

    switch (cmd) {
        case PC_CMD_READ_ID:
            SendIdReply();
            break;

        case PC_CMD_SET_RATE:
            if (len >= 5U) {
                uint32_t freq = (uint32_t)GetU16Be(&au8Pkt[3]);
                if (freq == 0UL) {
                    g_pcSampleFactor = 0UL;
                    g_pcUploadFlag   = 0U;
                } else {
                    uint32_t factor = (uint32_t)PC_SAMPLE_BASE_HZ / freq;
                    if (factor == 0UL) {
                        factor = 1UL;   /* freq > base: clamp to fastest */
                    }
                    g_pcSampleFactor = factor;
                    g_pcUploadFlag   = 1U;
                }
            }
            break;

        case PC_CMD_STOP_STREAM:
            g_pcUploadFlag = 0U;
            /* Counters are intentionally preserved (matches the original:
             * restarting mid-frame continues in the old slots). */
            break;

        case PC_CMD_WRITE_FLOAT:
            /* index is 1-based on the wire, 0-based in the registration API. */
            if (len >= 8U) {
                idx = au8Pkt[3];
                if ((idx >= 1U) && (idx <= PC_FLOAT_DL_NUM)) {
                    if (s_apFloatDl[idx - 1U] != NULL) {
                        *s_apFloatDl[idx - 1U] = GetF32Be(&au8Pkt[4]);
                    }
                }
            }
            break;

        case PC_CMD_MOTOR_START:
            if (s_pMotorStartStop != NULL) {
                *s_pMotorStartStop = 1U;
            }
            break;

        case PC_CMD_SPEED_UP:
            if (s_pSpeedRef != NULL) {
                *s_pSpeedRef += PC_SPEED_STEP_HZ;
            }
            break;

        case PC_CMD_SPEED_DOWN:
            if (s_pSpeedRef != NULL) {
                *s_pSpeedRef -= PC_SPEED_STEP_HZ;
            }
            break;

        case PC_CMD_MOTOR_STOP:
            if (s_pMotorStartStop != NULL) {
                *s_pMotorStartStop = 0U;
            }
            break;

        default:
            /* 0x00 / 0x02 / 0x04 and anything >= 0x0B: discard. */
            break;
    }
}

/**
 * @brief  Store a host packet for communication_handle(). Called from the USB
 *         interrupt (vcp_rxdata), copy-only, never blocks.
 */
void pc_communication_rx(const uint8_t *buf, uint32_t len)
{
    uint32_t i;
    uint32_t n = len;

    if (n > PC_RX_BUF_SIZE) {
        n = PC_RX_BUF_SIZE;
    }
    for (i = 0UL; i < n; i++) {
        s_au8RxBuf[i] = buf[i];
    }
    s_u16RxCount = (uint16_t)n;
}

/*******************************************************************************
 * Registration API
 ******************************************************************************/
void float_wave_upload_init(uint8_t index, float *data)
{
    if (index < PC_FLOAT_WAVE_NUM) {
        s_apFloatWave[index] = data;
    }
}

void int_wave_upload_init(uint8_t index, uint8_t type, int32_t *data)
{
    if (index < PC_INT_WAVE_NUM) {
        s_apIntWave[index]     = data;
        s_au8IntWaveType[index] = type;
    }
}

void float_data_upload_init(uint8_t index, float *data)
{
    if (index < PC_FLOAT_DATA_NUM) {
        s_apFloatData[index] = data;
    }
}

void int_data_upload_init(uint8_t index, uint8_t type, int32_t *data)
{
    if (index < PC_INT_DATA_NUM) {
        s_apIntData[index]     = data;
        s_au8IntDataType[index] = type;
    }
}

void float_data_download_init(uint8_t index, float *data)
{
    if (index < PC_FLOAT_DL_NUM) {
        s_apFloatDl[index] = data;
    }
}

void speed_ref_data_download_init(float *data)
{
    s_pSpeedRef = data;
}

void motor_start_stop_data_download_init(uint8_t *data)
{
    s_pMotorStartStop = data;
}

/*******************************************************************************
 * EOF (not truncated)
 ******************************************************************************/
