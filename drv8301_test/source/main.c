/**
 *******************************************************************************
 * @file  drv8301_test/source/main.c
 * @brief HC32F460KETA + DRV8301 hardware bring-up test firmware.
 *
 * This is a HARDWARE VERIFICATION program, not a FOC controller:
 *   1. DRV8301 SPI register write / read-back / fault status
 *   2. TMRA_1 3-PWM output to the DRV8301 (DRV8301 internal dead time)
 *   3. Open-loop V/f spin: rotor alignment + voltage/frequency ramp
 *   4. Two low-side shunt currents, bus voltage, NTC temperature, hall inputs
 *   5. USART3 115200 command console (single character commands)
 *
 * Board pin map (hardware/Netlist_Schematic1_2026-09-11.net):
 *   PWM_AH  PA8  (TIMA_1_PWM1, Func4)    PWM_AL PB13 (held low, 3-PWM mode)
 *   PWM_BH  PA9  (TIMA_1_PWM2, Func4)    PWM_BL PB14 (held low)
 *   PWM_CH  PA10 (TIMA_1_PWM3, Func4)    PWM_CL PB15 (held low)
 *   EN_GATE PB9   DC_CAL PB8   nFAULT PB2   nOCTW PC6   PWRGD PB5
 *   SPI: nSCS PA15, SCLK PC10, SDO PC11, SDI PC12
 *   ADC: PC0 VBUS (ch10, 95.3k/4.99k), PC1 IA (ch11), PC2 IB (ch12),
 *        PC3 NTC (ch13, 10k NTC + 4.7k pull-up)
 *   HALL: PA0 / PA1 / PA3 (10k pull-ups), KEY1 PD2, KEY2 PB3, KEY3 PB4,
 *         LED4 PC9 (active low)
 *
 * Safety notes:
 *   - EN_GATE is kept low (all gates off) whenever the motor is not spinning.
 *   - The DRV8301 is configured for 3-PWM mode.  The INL pins are driven low,
 *     they are "don't care" in 3-PWM mode.  This lets the DRV8301 insert its
 *     own dead time (RDTC = 1 ohm on this board, ~50 ns).
 *   - If the SPI read-back check fails, EN_GATE stays low and the motor can
 *     never be started.
 *   - Software over-current trip + current foldback on the modulation index.
 *******************************************************************************
 * Copyright (C) 2022-2025, Xiaohua Semiconductor Co., Ltd. All rights reserved.
 *
 * This software component is licensed by XHSC under BSD 3-Clause license
 *******************************************************************************
 */

#include "main.h"
#include <stdio.h>
#include <math.h>

/*******************************************************************************
 * Local pre-processor symbols/macros ('#define')
 ******************************************************************************/
/* Peripheral register WE/WP selection.
   NOTE: LL_PERIPH_FCG must stay here, otherwise CLK_SetSysClockSrc(PLL) traps
   in DDL_AssertHandler() because PWC_FCG0PC.PRT0 is write protected. */
#define LL_PERIPH_SEL                   (LL_PERIPH_GPIO | LL_PERIPH_FCG | LL_PERIPH_PWC_CLK_RMU | \
                                         LL_PERIPH_EFM | LL_PERIPH_SRAM)

/*******************************************************************************
 * Board pin map
 ******************************************************************************/
/* DRV8301 SPI: hardware SPI2, mode 1 (SDI sampled on falling SCLK edge).
   PC10/PC11/PC12 are in Func_Grp1, so SPI2 maps onto them directly:
     SPI2_SCK  = Func47, SPI2_MISO = Func45, SPI2_MOSI = Func44.
   nSCS stays a plain GPIO (PA15): each 16-bit frame is framed by hand. */
#define SPI_CS_PORT                 (GPIO_PORT_A)
#define SPI_CS_PIN                  (GPIO_PIN_15)
#define SPI_SCK_PORT                (GPIO_PORT_C)
#define SPI_SCK_PIN                 (GPIO_PIN_10)
#define SPI_SCK_FUNC                (GPIO_FUNC_47)
#define SPI_MISO_PORT               (GPIO_PORT_C)
#define SPI_MISO_PIN                (GPIO_PIN_11)
#define SPI_MISO_FUNC               (GPIO_FUNC_45)
#define SPI_MOSI_PORT               (GPIO_PORT_C)
#define SPI_MOSI_PIN                (GPIO_PIN_12)
#define SPI_MOSI_FUNC               (GPIO_FUNC_44)
#define SPI_UNIT                    (CM_SPI2)
#define SPI_FCG_ENABLE()            (FCG_Fcg1PeriphClockCmd(FCG1_PERIPH_SPI2, ENABLE))
#define SPI_BR_FAST                 (SPI_BR_CLK_DIV64)      /* 100 MHz / 64 = 1.5625 MHz */
#define SPI_BR_SLOW                 (SPI_BR_CLK_DIV256)     /* 100 MHz / 256 = 390 kHz */
#define SPI_TIMEOUT                 (0x200000UL)

/* DRV8301 control / status */
#define EN_GATE_PORT                (GPIO_PORT_B)
#define EN_GATE_PIN                 (GPIO_PIN_09)
#define DC_CAL_PORT                 (GPIO_PORT_B)
#define DC_CAL_PIN                  (GPIO_PIN_08)
#define NFAULT_PORT                 (GPIO_PORT_B)
#define NFAULT_PIN                  (GPIO_PIN_02)
#define NOCTW_PORT                  (GPIO_PORT_C)
#define NOCTW_PIN                   (GPIO_PIN_06)
#define PWRGD_PORT                  (GPIO_PORT_B)
#define PWRGD_PIN                   (GPIO_PIN_05)

/* Power stage PWM (TMRA_1, function 4 on all six pins) */
#define PWM_AH_PORT                 (GPIO_PORT_A)
#define PWM_AH_PIN                  (GPIO_PIN_08)
#define PWM_BH_PORT                 (GPIO_PORT_A)
#define PWM_BH_PIN                  (GPIO_PIN_09)
#define PWM_CH_PORT                 (GPIO_PORT_A)
#define PWM_CH_PIN                  (GPIO_PIN_10)
#define PWM_INL_PORT                (GPIO_PORT_B)
#define PWM_INL_PIN                 (GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15)

/* Hall inputs */
#define HALL_A_PORT                 (GPIO_PORT_A)
#define HALL_A_PIN                  (GPIO_PIN_00)
#define HALL_B_PORT                 (GPIO_PORT_A)
#define HALL_B_PIN                  (GPIO_PIN_01)
#define HALL_C_PORT                 (GPIO_PORT_A)
#define HALL_C_PIN                  (GPIO_PIN_03)

/* Keys / LED */
#define KEY1_PORT                   (GPIO_PORT_D)
#define KEY1_PIN                    (GPIO_PIN_02)
#define KEY2_PORT                   (GPIO_PORT_B)
#define KEY2_PIN                    (GPIO_PIN_03)
#define KEY3_PORT                   (GPIO_PORT_B)
#define KEY3_PIN                    (GPIO_PIN_04)
#define LED_PORT                    (GPIO_PORT_C)
#define LED_PIN                     (GPIO_PIN_09)

/* USART3 debug console */
#define USART_RX_PORT               (GPIO_PORT_B)
#define USART_RX_PIN                (GPIO_PIN_07)
#define USART_RX_GPIO_FUNC          (GPIO_FUNC_33)
#define USART_TX_PORT               (GPIO_PORT_B)
#define USART_TX_PIN                (GPIO_PIN_06)
#define USART_TX_GPIO_FUNC          (GPIO_FUNC_32)
#define USART_UNIT                  (CM_USART3)
#define USART_FCG_ENABLE()          (FCG_Fcg1PeriphClockCmd(FCG1_PERIPH_USART3, ENABLE))
#define USART_RX_ERR_IRQn           (INT000_IRQn)
#define USART_RX_ERR_INT_SRC        (INT_SRC_USART3_EI)
#define USART_RX_FULL_IRQn          (INT001_IRQn)
#define USART_RX_FULL_INT_SRC       (INT_SRC_USART3_RI)

#define XTAL_PORT                   (GPIO_PORT_H)
#define XTAL_PIN                    (GPIO_PIN_00 | GPIO_PIN_01)

#define RING_BUF_SIZE               (500UL)

/*******************************************************************************
 * Application parameters
 ******************************************************************************/
#define PWM_FREQ_HZ                 (20000UL)
#define PWM_PERIOD                  (100000000UL / (2UL * PWM_FREQ_HZ))   /* 2500 */

/* Current sense: 20 mOhm shunts (R40/R48), DRV8301 gain 10 V/V.
   One ADC count = 3.3V / 4095 / (0.020 * 10) = 4.028 mA. */
#define SHUNT_OHM                   (0.020f)
#define AMP_GAIN                    (10.0f)
#define ADC_VREF                    (3.3f)
#define ADC_FULL_SCALE              (4095.0f)

/* Total bus current: U9 TP181A1 (G = 50 V/V) across R57 = 5 mOhm.
   Vout = 1.65 V - I * 0.25 V/A (falls with positive current), range
   +/-6.6 A.  C34 (22 uF) across the shunt bypasses the 20 kHz ripple, so
   this channel reads the average input current.  Output -> PA2 / ADC1_IN2. */
#define ITOTAL_SHUNT_OHM            (0.005f)
#define ITOTAL_GAIN                 (50.0f)
#define ITOTAL_TRIP_A               (5.5f)      /* independent bus trip */
#define ITOTAL_TRIP_CNT             (20U)       /* 20 kHz samples -> ~1 ms */

/* Bus voltage divider: 95.3k / 4.99k -> 0.049756 */
#define VBUS_DIV                    (0.049756f)

/* NTC: 10k at 25C, 4.7k pull-up to 3.3V, Beta = 3950 */
#define NTC_PULLUP_OHM              (4700.0f)
#define NTC_R25_OHM                 (10000.0f)
#define NTC_BETA                    (3950.0f)

#define VBUS_MIN_START              (8.0f)      /* V, DRV8301 needs PVDD >= 6V */
#define VBUS_MAX_RUN                (60.0f)
#define I_SOFT_LIMIT_A              (3.0f)      /* modulation foldback threshold */
#define I_TRIP_A                    (6.0f)      /* hard over-current trip */
#define I_TRIP_CNT                  (5U)        /* consecutive samples */

#define FREQ_MIN_HZ                 (0.5f)
#define FREQ_MAX_HZ                 (60.0f)
#define MOD_MIN                     (0.010f)
#define MOD_MAX                     (0.600f)

#define FREQ_SLEW_UP                (4.0f)      /* Hz per second */
#define FREQ_SLEW_DN                (8.0f)
#define MOD_SLEW_UP                 (0.15f)     /* per second */
#define MOD_SLEW_DN                 (0.30f)
#define ALIGN_TIME_MS               (300UL)

/* Periodic status line.  1 Hz keeps the console readable; use 'm' to toggle,
   'h' for a one-shot status line. */
#define MONITOR_PERIOD_MS           (1000UL)

#define TWO_PI_F                    (6.283185307f)
#define SIN_TAB_SIZE                (256U)

/*******************************************************************************
 * Types
 ******************************************************************************/
typedef enum {
    ST_IDLE = 0,
    ST_ALIGN,
    ST_RAMP,
    ST_SPIN,
    ST_STOP,
    ST_FAULT
} run_state_t;

/*******************************************************************************
 * Local variables
 ******************************************************************************/
static uint8_t m_au8DataBuf[RING_BUF_SIZE];
static stc_ring_buf_t m_stcRingBuf;

/* PWM / control */
static volatile run_state_t s_enState = ST_IDLE;
static volatile float s_fFreqCmd = 3.0f;        /* electrical Hz */
static volatile float s_fModCmd = 0.10f;        /* modulation index 0..MOD_MAX */
static volatile float s_fFreq = 0.0f;
static volatile float s_fMod = 0.0f;
static volatile float s_fModScale = 1.0f;       /* current foldback factor */
static volatile float s_fTheta = 0.0f;          /* electrical angle, rad */
static volatile uint32_t s_u32PwmCnt = 0UL;
static volatile uint8_t s_u8Tick1ms = 0U;
static uint32_t s_u32Ms = 0UL;
static uint32_t s_u32AlignMs = 0UL;
static uint8_t s_u8OcCnt = 0U;
static uint16_t s_u16AdcAliveMs = 0U;

/* measurements */
static volatile uint8_t s_u8AdcBusy = 0U;
static uint16_t s_u16RawVbus = 0U;
static uint16_t s_u16RawIa = 0U;
static uint16_t s_u16RawIb = 0U;
static uint16_t s_u16RawNtc = 0U;
static float s_fIaOfs = 2048.0f;
static float s_fIbOfs = 2048.0f;
static float s_fVbus = 0.0f;
static float s_fIa = 0.0f;
static float s_fIb = 0.0f;
static float s_fIamp = 0.0f;
static uint16_t s_u16RawItotal = 0U;
static float s_fItotalOfs = 2048.0f;    /* TP181 zero-current ADC counts */
static float s_fItotal = 0.0f;          /* average bus current, A */
static uint8_t s_u8ItotalTripCnt = 0U;
static float s_fTempC = 0.0f;
static uint8_t s_u8TempValid = 0U;

/* DRV8301 */
static uint8_t s_u8DrvOk = 0U;          /* 1 while EN_GATE is high and the SPI is verified */
static uint8_t s_u8DrvBootOk = 0U;      /* result captured at power-up (for the banner) */
static uint8_t s_u8PwmReady = 0U;       /* TMRA timer + OVF interrupt are running */
static uint16_t s_u16DrvSr1 = 0U;
static uint16_t s_u16DrvSr2 = 0U;
static uint8_t s_u8EnGateHold = 0U;     /* 'e': keep EN_GATE high for measurements */

/* console / keys */
static uint8_t s_u8Monitor = 1U;
static uint8_t s_u8LedOn = 0U;
static uint8_t s_u8Key1Stable = 0U, s_u8Key2Stable = 0U, s_u8Key3Stable = 0U;
static uint8_t s_u8Key1Cnt = 0U, s_u8Key2Cnt = 0U, s_u8Key3Cnt = 0U;

static float s_afSin[SIN_TAB_SIZE];

/*******************************************************************************
 * Function prototypes
 ******************************************************************************/
static void SystemClockInit(void);
static void UART_Init(void);
static void UART_SendByte(uint8_t u8Data);
static uint32_t UART_ReadByte(uint8_t *pu8Data);
static void GpioInit(void);
static void Spi2Init(uint32_t u32Baud);
static uint16_t Spi2Transfer16(uint16_t u16Tx);
static void AdcInit(void);
static void AdcCalItotal(void);
static void AdcCalibrate(void);
static uint8_t AdcReadBlocking(uint16_t *pIa, uint16_t *pIb);
static uint8_t AdcReadBlockingSafe(uint16_t *pIa, uint16_t *pIb);
static void PwmInit(void);
static void PwmSetDuty(float fA, float fB, float fC);
static void Drv8301Init(uint8_t u8Verbose);
static uint8_t Drv8301SpiWake(void);
static void Drv8301Dump(void);
static void Drv8301SpiScan(void);
static void EnGateHoldToggle(void);
static void Drv8301PrintFaults(uint16_t u16Sr1, uint16_t u16Sr2);
static void Drv8301ClearFaults(void);
static void MotorStart(void);
static void MotorStop(void);
static void EnterFault(const char *pszReason);
static void ControlTick(void);
static void UpdateMeasurements(void);
static void ConsolePoll(void);
static void PrintHelp(void);
static void PrintStatus(void);
static void PrintF1(float fVal);
static void PrintF2(float fVal);
static uint8_t HallRead(void);
static float SinTab(float fIdx);
static uint8_t KeyPressed(uint8_t u8Port, uint16_t u16Pin, uint8_t *pu8Stable, uint8_t *pu8Cnt);

/*******************************************************************************
 * DRV8301 definitions
 ******************************************************************************/
#define DRV_REG_SR1                 (0U)
#define DRV_REG_SR2                 (1U)
#define DRV_REG_CR1                 (2U)
#define DRV_REG_CR2                 (3U)

/* Control Register 1: gate 0.7A | 3-PWM inputs | OC latch shutdown | 0.317V */
#define DRV_CR1_GATE_0_7A           (0x0001U)
#define DRV_CR1_GATE_RESET          (0x0004U)
#define DRV_CR1_PWM_MODE_3          (0x0008U)
#define DRV_CR1_OCP_LATCH           (0x0010U)
#define DRV_CR1_OC_ADJ_14           (0x0380U)
#define DRV_CR1_VAL                 (DRV_CR1_GATE_0_7A | DRV_CR1_PWM_MODE_3 | \
                                     DRV_CR1_OCP_LATCH | DRV_CR1_OC_ADJ_14)

/* Control Register 2: OT+OC on nOCTW, gain 10 V/V, DC_CAL normal, cycle-by-cycle */
#define DRV_CR2_VAL                 (0x0000U)

/* bit helpers */
#define DRV_SR1_FAULT               (0x0400U)
#define DRV_SR1_GVDD_UV             (0x0200U)
#define DRV_SR1_PVDD_UV             (0x0100U)
#define DRV_SR1_OTSD                (0x0080U)
#define DRV_SR1_OTW                 (0x0040U)

/*******************************************************************************
 * GPIO macros
 ******************************************************************************/
#define DRV_CS_LOW()                GPIO_ResetPins(SPI_CS_PORT, SPI_CS_PIN)
#define DRV_CS_HIGH()               GPIO_SetPins(SPI_CS_PORT, SPI_CS_PIN)

#define EN_GATE_LOW()               GPIO_ResetPins(EN_GATE_PORT, EN_GATE_PIN)
#define EN_GATE_HIGH()              GPIO_SetPins(EN_GATE_PORT, EN_GATE_PIN)
#define DC_CAL_LOW()                GPIO_ResetPins(DC_CAL_PORT, DC_CAL_PIN)
#define DC_CAL_HIGH()               GPIO_SetPins(DC_CAL_PORT, DC_CAL_PIN)
#define NFAULT_LOW()                (GPIO_ReadInputPins(NFAULT_PORT, NFAULT_PIN) == PIN_RESET)
#define NOCTW_LOW()                 (GPIO_ReadInputPins(NOCTW_PORT, NOCTW_PIN) == PIN_RESET)

#define LED_ON()                    GPIO_ResetPins(LED_PORT, LED_PIN)
#define LED_OFF()                   GPIO_SetPins(LED_PORT, LED_PIN)

/*******************************************************************************
 * Function implementation
 ******************************************************************************/

/**
 * @brief  nSCS high time between two 16-bit frames (>= tCSH of the DRV8301).
 */
static void SpiDelay(void)
{
    DDL_DelayUS(1UL);
}

/**
 * @brief  System clock: 16 MHz XTAL -> MPLL 200 MHz, PCLK1 100 MHz.
 */
static void SystemClockInit(void)
{
    stc_clock_xtal_init_t stcXtalInit;
    stc_clock_pll_init_t  stcMpllInit;
    uint32_t u32Wait;

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
        for (;;) {
        }
    }

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
        for (;;) {
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
 * @brief  USART3 TX, blocking.
 */
static void UART_SendByte(uint8_t u8Data)
{
    while (SET != USART_GetStatus(USART_UNIT, USART_FLAG_TX_EMPTY)) {
        ;
    }
    USART_WriteData(USART_UNIT, (uint16_t)u8Data);
}

/**
 * @brief  Read one byte from the RX ring buffer (non-blocking).
 */
static uint32_t UART_ReadByte(uint8_t *pu8Data)
{
    return BUF_Read(&m_stcRingBuf, pu8Data, 1UL);
}

/**
 * @brief  Retarget printf to USART3.
 */
int fputc(int ch, FILE *f)
{
    (void)f;
    UART_SendByte((uint8_t)ch);
    return ch;
}

static void USART_RxFull_IrqCallback(void)
{
    uint8_t u8Data = (uint8_t)USART_ReadData(USART_UNIT);
    (void)BUF_Write(&m_stcRingBuf, &u8Data, 1UL);
}

static void USART_RxError_IrqCallback(void)
{
    (void)USART_ReadData(USART_UNIT);
    USART_ClearStatus(USART_UNIT, (USART_FLAG_PARITY_ERR | USART_FLAG_FRAME_ERR | USART_FLAG_OVERRUN));
}

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
 * @brief  USART3 115200-8-N-1 + RX interrupt.
 */
static void UART_Init(void)
{
    stc_usart_uart_init_t stcUartInit;
    stc_irq_signin_config_t stcIrqSigninConfig;

    GPIO_SetFunc(USART_RX_PORT, USART_RX_PIN, USART_RX_GPIO_FUNC);
    GPIO_SetFunc(USART_TX_PORT, USART_TX_PIN, USART_TX_GPIO_FUNC);

    USART_FCG_ENABLE();

    (void)BUF_Init(&m_stcRingBuf, m_au8DataBuf, sizeof(m_au8DataBuf));

    (void)USART_UART_StructInit(&stcUartInit);
    stcUartInit.u32ClockDiv     = USART_CLK_DIV64;      /* PCLK1 = 100 MHz */
    stcUartInit.u32Baudrate     = 115200UL;
    stcUartInit.u32OverSampleBit = USART_OVER_SAMPLE_8BIT;
    if (LL_OK != USART_UART_Init(USART_UNIT, &stcUartInit, NULL)) {
        for (;;) {
        }
    }

    stcIrqSigninConfig.enIRQn = USART_RX_ERR_IRQn;
    stcIrqSigninConfig.enIntSrc = USART_RX_ERR_INT_SRC;
    stcIrqSigninConfig.pfnCallback = &USART_RxError_IrqCallback;
    INTC_IrqInstalHandler(&stcIrqSigninConfig, DDL_IRQ_PRIO_DEFAULT);

    stcIrqSigninConfig.enIRQn = USART_RX_FULL_IRQn;
    stcIrqSigninConfig.enIntSrc = USART_RX_FULL_INT_SRC;
    stcIrqSigninConfig.pfnCallback = &USART_RxFull_IrqCallback;
    INTC_IrqInstalHandler(&stcIrqSigninConfig, DDL_IRQ_PRIO_DEFAULT);

    USART_FuncCmd(USART_UNIT, (USART_TX | USART_RX | USART_INT_RX), ENABLE);
}

/**
 * @brief  All board GPIO.
 */
static void GpioInit(void)
{
    stc_gpio_init_t stcGpioInit;

    /*
     * PA13/PA14/PA15/PB3/PB4 come out of reset as JTAG/SWD pins and
     * PSPCR.SPFE[4:0] = 0x1F. SPFE has priority over PFSR.FSEL, so a plain
     * GPIO_Init() on these pins has no effect on the pad at all.
     * Board uses PA15 = nSCS, PB3 = KEY2, PB4 = KEY3 -> release JTAG but
     * keep SWD (SPFE[0]=JTCK_SWCLK, SPFE[1]=JTMS_SWDIO) alive.
     * RM 9.4.7 "SPFE[4:0] ... priority higher than PFSRxy.FSEL[5:0]",
     * RM 9.4.12 note "PA13/PA14/PA15/PB3/PB4 ... clear PSPCR first".
     */
    GPIO_SetDebugPort((uint8_t)(GPIO_PIN_TDI | GPIO_PIN_TDO | GPIO_PIN_TRST), DISABLE);

    /* ---- outputs first: EN_GATE low, DC_CAL low, LED off, SPI idle ---- */
    (void)GPIO_StructInit(&stcGpioInit);
    stcGpioInit.u16PinDir       = PIN_DIR_OUT;
    stcGpioInit.u16PinOutputType = PIN_OUT_TYPE_CMOS;
    stcGpioInit.u16PinDrv       = PIN_LOW_DRV;
    stcGpioInit.u16PinState     = PIN_RESET;
    stcGpioInit.u16PullUp       = PIN_PU_OFF;
    stcGpioInit.u16PinAttr      = PIN_ATTR_DIGITAL;

    (void)GPIO_Init(EN_GATE_PORT, EN_GATE_PIN, &stcGpioInit);
    (void)GPIO_Init(DC_CAL_PORT, DC_CAL_PIN, &stcGpioInit);

    /* INL pins: low. DRV8301 runs in 3-PWM mode, INL is don't care. */
    (void)GPIO_Init(PWM_INL_PORT, PWM_INL_PIN, &stcGpioInit);

    /* PWM high-side pins: low until the timer is started (GPIO mode). */
    (void)GPIO_Init(PWM_AH_PORT, PWM_AH_PIN, &stcGpioInit);
    (void)GPIO_Init(PWM_BH_PORT, PWM_BH_PIN, &stcGpioInit);
    (void)GPIO_Init(PWM_CH_PORT, PWM_CH_PIN, &stcGpioInit);

    /* SPI nSCS: plain GPIO, idle high. */
    stcGpioInit.u16PinState = PIN_SET;
    (void)GPIO_Init(SPI_CS_PORT, SPI_CS_PIN, &stcGpioInit);

    /* LED is active low. */
    stcGpioInit.u16PinState = PIN_SET;
    (void)GPIO_Init(LED_PORT, LED_PIN, &stcGpioInit);

    /* ---- inputs ---- */
    (void)GPIO_StructInit(&stcGpioInit);
    stcGpioInit.u16PinDir   = PIN_DIR_IN;
    stcGpioInit.u16PullUp   = PIN_PU_ON;
    stcGpioInit.u16PinAttr  = PIN_ATTR_DIGITAL;

    (void)GPIO_Init(NFAULT_PORT, NFAULT_PIN, &stcGpioInit);
    (void)GPIO_Init(NOCTW_PORT, NOCTW_PIN, &stcGpioInit);
    (void)GPIO_Init(PWRGD_PORT, PWRGD_PIN, &stcGpioInit);

    (void)GPIO_Init(HALL_A_PORT, HALL_A_PIN, &stcGpioInit);
    (void)GPIO_Init(HALL_B_PORT, HALL_B_PIN, &stcGpioInit);
    (void)GPIO_Init(HALL_C_PORT, HALL_C_PIN, &stcGpioInit);

    (void)GPIO_Init(KEY1_PORT, KEY1_PIN, &stcGpioInit);
    (void)GPIO_Init(KEY2_PORT, KEY2_PIN, &stcGpioInit);
    (void)GPIO_Init(KEY3_PORT, KEY3_PIN, &stcGpioInit);

    /* ---- SPI2 alternate function (Func_Grp1) ----
       The peripheral now owns SCK/MISO/MOSI; direction comes from SPI2.
       GPIO_SetFunc() must run while the GPIO registers are unlocked. */
    (void)GPIO_StructInit(&stcGpioInit);
    stcGpioInit.u16PinDrv  = PIN_HIGH_DRV;
    stcGpioInit.u16PinAttr = PIN_ATTR_DIGITAL;
    (void)GPIO_Init(SPI_SCK_PORT,  SPI_SCK_PIN,  &stcGpioInit);
    (void)GPIO_Init(SPI_MISO_PORT, SPI_MISO_PIN, &stcGpioInit);
    (void)GPIO_Init(SPI_MOSI_PORT, SPI_MOSI_PIN, &stcGpioInit);

    GPIO_SetFunc(SPI_SCK_PORT,  SPI_SCK_PIN,  SPI_SCK_FUNC);
    GPIO_SetFunc(SPI_MISO_PORT, SPI_MISO_PIN, SPI_MISO_FUNC);
    GPIO_SetFunc(SPI_MOSI_PORT, SPI_MOSI_PIN, SPI_MOSI_FUNC);
}

/* ============================ DRV8301 SPI ================================ */

/**
 * @brief  Configure SPI2 as 16-bit master, mode 1, MSB first.
 * @param  [in] u32Baud  SPI_BR_CLK_DIVx (PCLK1 = 100 MHz)
 */
static void Spi2Init(uint32_t u32Baud)
{
    stc_spi_init_t stcSpiInit;

    SPI_FCG_ENABLE();
    SPI_Cmd(SPI_UNIT, DISABLE);

    (void)SPI_StructInit(&stcSpiInit);
    stcSpiInit.u32WireMode          = SPI_4_WIRE;    /* NSS is not routed; MODFE off */
    stcSpiInit.u32TransMode         = SPI_FULL_DUPLEX;
    stcSpiInit.u32MasterSlave       = SPI_MASTER;
    stcSpiInit.u32ModeFaultDetect   = SPI_MD_FAULT_DETECT_DISABLE;
    stcSpiInit.u32Parity            = SPI_PARITY_INVD;
    stcSpiInit.u32SpiMode           = SPI_MD_1;      /* CPOL=0, CPHA=1 */
    stcSpiInit.u32BaudRatePrescaler = u32Baud;
    stcSpiInit.u32DataBits          = SPI_DATA_SIZE_16BIT;
    stcSpiInit.u32FirstBit          = SPI_FIRST_MSB;
    stcSpiInit.u32FrameLevel        = SPI_1_FRAME;
    (void)SPI_Init(SPI_UNIT, &stcSpiInit);
    SPI_Cmd(SPI_UNIT, ENABLE);
}

/**
 * @brief  Full-duplex 16-bit transfer (one DRV8301 frame).
 * @retval Received word, or 0 if the peripheral did not complete in time.
 */
static uint16_t Spi2Transfer16(uint16_t u16Tx)
{
    uint32_t u32Timeout;

    u32Timeout = SPI_TIMEOUT;
    while (SPI_GetStatus(SPI_UNIT, SPI_FLAG_TX_BUF_EMPTY) == RESET) {
        if (--u32Timeout == 0UL) {
            return 0U;
        }
    }

    SPI_WriteData(SPI_UNIT, (uint32_t)u16Tx);

    u32Timeout = SPI_TIMEOUT;
    while (SPI_GetStatus(SPI_UNIT, SPI_FLAG_RX_BUF_FULL) == RESET) {
        if (--u32Timeout == 0UL) {
            return 0U;
        }
    }

    return (uint16_t)SPI_ReadData(SPI_UNIT);
}

static void Drv8301Write(uint8_t u8Addr, uint16_t u16Data)
{
    uint16_t u16Frame;

    u16Frame = (uint16_t)(((uint16_t)(u8Addr & 0x03U) << 11U) | (u16Data & 0x07FFU));

    DRV_CS_LOW();
    SpiDelay();                 /* nSCS setup time before the first SCLK edge */
    (void)Spi2Transfer16(u16Frame);
    DRV_CS_HIGH();
    SpiDelay();
}

static uint16_t Drv8301Read(uint8_t u8Addr)
{
    uint16_t u16Frame;
    uint16_t u16Data;

    /* Frame 1: read command. */
    u16Frame = (uint16_t)(0x8000U | ((uint16_t)(u8Addr & 0x03U) << 11U));

    DRV_CS_LOW();
    SpiDelay();
    (void)Spi2Transfer16(u16Frame);
    DRV_CS_HIGH();
    SpiDelay();

    /* Frame 2: the response word is loaded when nSCS goes low. */
    DRV_CS_LOW();
    SpiDelay();
    u16Data = Spi2Transfer16(0x0000U);
    DRV_CS_HIGH();
    SpiDelay();

    return u16Data;
}

static void Drv8301PrintFaults(uint16_t u16Sr1, uint16_t u16Sr2)
{
    printf("  SR1=%04X SR2=%04X", (unsigned)u16Sr1, (unsigned)u16Sr2);
    if ((u16Sr1 & DRV_SR1_FAULT) != 0U)   { printf(" FAULT"); }
    if ((u16Sr1 & DRV_SR1_GVDD_UV) != 0U) { printf(" GVDD_UV"); }
    if ((u16Sr1 & DRV_SR1_PVDD_UV) != 0U) { printf(" PVDD_UV"); }
    if ((u16Sr1 & DRV_SR1_OTSD) != 0U)    { printf(" OTSD"); }
    if ((u16Sr1 & DRV_SR1_OTW) != 0U)     { printf(" OTW"); }
    if ((u16Sr1 & 0x0020U) != 0U)         { printf(" FETHA_OC"); }
    if ((u16Sr1 & 0x0010U) != 0U)         { printf(" FETLA_OC"); }
    if ((u16Sr1 & 0x0008U) != 0U)         { printf(" FETHB_OC"); }
    if ((u16Sr1 & 0x0004U) != 0U)         { printf(" FETLB_OC"); }
    if ((u16Sr1 & 0x0002U) != 0U)         { printf(" FETHC_OC"); }
    if ((u16Sr1 & 0x0001U) != 0U)         { printf(" FETLC_OC"); }
    printf("\r\n");
}

static void Drv8301ClearFaults(void)
{
    Drv8301Write(DRV_REG_CR1, (uint16_t)(DRV_CR1_VAL | DRV_CR1_GATE_RESET));
    DDL_DelayMS(1UL);
    Drv8301Write(DRV_REG_CR1, DRV_CR1_VAL);
}

/**
 * @brief  Ensure EN_GATE is high. A full EN_GATE low (>20 us) powers down the
 *         charge pump, the current shunt amplifiers and the SPI registers.
 * @retval 1: EN_GATE had to be raised (caller should lower it again)
 *         0: EN_GATE was already high
 */
static uint8_t Drv8301SpiWake(void)
{
    if (GPIO_ReadOutputPins(EN_GATE_PORT, EN_GATE_PIN) == PIN_SET) {
        return 0U;
    }

    EN_GATE_HIGH();
    DDL_DelayMS(12UL);          /* power-up sequence / tSPI_READY */
    return 1U;
}

/**
 * @brief  Configure and verify the DRV8301. EN_GATE must already be high.
 * @param  [in] u8Verbose       1: print the read-back values, 0: quiet
 */
static void Drv8301Init(uint8_t u8Verbose)
{
    uint16_t u16Cr1;
    uint16_t u16Cr2;

    s_u8DrvOk = 0U;

    Drv8301Write(DRV_REG_CR1, DRV_CR1_VAL);
    Drv8301Write(DRV_REG_CR2, DRV_CR2_VAL);
    DDL_DelayMS(1UL);

    u16Cr1 = Drv8301Read(DRV_REG_CR1);
    u16Cr2 = Drv8301Read(DRV_REG_CR2);
    s_u16DrvSr1 = Drv8301Read(DRV_REG_SR1);
    s_u16DrvSr2 = Drv8301Read(DRV_REG_SR2);

    if (u8Verbose != 0U) {
        printf("DRV8301 CR1=%04X (expect %04X) CR2=%04X (expect %04X)\r\n",
               (unsigned)u16Cr1, (unsigned)DRV_CR1_VAL,
               (unsigned)u16Cr2, (unsigned)DRV_CR2_VAL);
    }

    if (((u16Cr1 & 0x07FFU) == DRV_CR1_VAL) && ((u16Cr2 & 0x07FFU) == DRV_CR2_VAL)) {
        s_u8DrvOk = 1U;
        if (u8Verbose != 0U) {
            printf("DRV8301: SPI OK\r\nDRV8301 status:");
            Drv8301PrintFaults(s_u16DrvSr1, s_u16DrvSr2);
        }
    } else {
        if (u8Verbose != 0U) {
            printf("DRV8301: SPI FAILED - check PVDD, SPI wiring, EN_GATE\r\n");
        }
    }
}

static void Drv8301Dump(void)
{
    uint16_t u16Sr1;
    uint16_t u16Sr2;
    uint16_t u16Cr1;
    uint16_t u16Cr2;
    uint8_t u8Raised;

    /* While idle EN_GATE is low and the device is powered down. Wake it up
       for the diagnostic read; the PWM is at 50 % on all phases (no torque). */
    u8Raised = Drv8301SpiWake();

    u16Sr1 = Drv8301Read(DRV_REG_SR1);
    u16Sr2 = Drv8301Read(DRV_REG_SR2);
    u16Cr1 = Drv8301Read(DRV_REG_CR1);
    u16Cr2 = Drv8301Read(DRV_REG_CR2);

    s_u16DrvSr1 = u16Sr1;
    s_u16DrvSr2 = u16Sr2;

    printf("DRV: SR1=%04X SR2=%04X CR1=%04X CR2=%04X nFAULT=%u nOCTW=%u PWRGD=%u\r\n",
           (unsigned)u16Sr1, (unsigned)u16Sr2,
           (unsigned)u16Cr1, (unsigned)u16Cr2,
           (unsigned)(NFAULT_LOW() ? 0U : 1U),
           (unsigned)(NOCTW_LOW() ? 0U : 1U),
           (unsigned)((GPIO_ReadInputPins(PWRGD_PORT, PWRGD_PIN) == PIN_SET) ? 1U : 0U));
    Drv8301PrintFaults(u16Sr1, u16Sr2);

    if ((u8Raised != 0U) && (s_u8EnGateHold == 0U) &&
        (s_enState != ST_ALIGN) && (s_enState != ST_RAMP) &&
        (s_enState != ST_SPIN) && (s_enState != ST_STOP)) {
        EN_GATE_LOW();          /* back to idle, all gates off */
        s_u8DrvOk = 0U;
    }
}

/**
 * @brief  'e': keep EN_GATE high so the DRV8301 supply pins can be measured
 *         with a multimeter.  The PWM stays at 50 % on all three phases, so
 *         the phase-to-phase voltage is zero: no torque, no bus current.
 */
static void EnGateHoldToggle(void)
{
    if (s_u8EnGateHold == 0U) {
        s_u8EnGateHold = 1U;
        EN_GATE_HIGH();
        s_u8DrvOk = 0U;
        printf("\r\nEN_GATE held HIGH (PWM 50%% on all phases -> no torque).\r\n");
        printf("Measure at U1 (DRV8301, top view, pin 1 = dot):\r\n");
        printf("  EN_GATE (16)     = 2.9 .. 3.3 V\r\n");
        printf("  PVDD    (29/53/54) = Vbus (same net as the ADC divider)\r\n");
        printf("  VDD_SPI (49)     = 3.3 V\r\n");
        printf("  DVDD    (23)     = 3.0 .. 3.6 V\r\n");
        printf("  AVDD    (27)     = 5.5 .. 7.0 V\r\n");
        printf("  GVDD    (13)     = 8 .. 16 V (charge pump)\r\n");
        printf("'d' = read registers   't' = slow SPI scan   'e' = release\r\n\r\n");
    } else {
        s_u8EnGateHold = 0U;
        EN_GATE_LOW();
        s_u8DrvOk = 0U;
        printf("EN_GATE released (LOW).\r\n");
    }
}

/**
 * @brief  't': read all four registers at PCLK1/256 = 390 kHz to rule out any
 *         SPI clock/timing problem, then restore the normal 1.5625 MHz clock.
 */
static void Drv8301SpiScan(void)
{
    uint8_t u8Raised;
    uint8_t i;
    uint16_t u16Val;

    u8Raised = Drv8301SpiWake();

    printf("SPI2 slow scan (PCLK1/256 = 390 kHz), nSCS = PA15 GPIO\r\n");

    Spi2Init(SPI_BR_SLOW);
    for (i = 0U; i < 4U; i++) {
        u16Val = Drv8301Read(i);
        printf("  slow read [%u] = %04X\r\n", (unsigned)i, (unsigned)u16Val);
    }
    Spi2Init(SPI_BR_FAST);

    printf("  (SR1=0 SR2=1 CR1=2 CR2=3; CR1 should be %04X, CR2 %04X)\r\n",
           (unsigned)DRV_CR1_VAL, (unsigned)DRV_CR2_VAL);

    if ((u8Raised != 0U) && (s_u8EnGateHold == 0U)) {
        EN_GATE_LOW();
        s_u8DrvOk = 0U;
    }
}

/* ============================== ADC ====================================== */

static void AdcInit(void)
{
    stc_adc_init_t stcAdcInit;
    stc_gpio_init_t stcGpioInit;

    FCG_Fcg3PeriphClockCmd(FCG3_PERIPH_ADC1, ENABLE);

    /* PC0..PC3 as analog inputs. */
    (void)GPIO_StructInit(&stcGpioInit);
    stcGpioInit.u16PinAttr = PIN_ATTR_ANALOG;
    (void)GPIO_Init(GPIO_PORT_C, (GPIO_PIN_00 | GPIO_PIN_01 | GPIO_PIN_02 | GPIO_PIN_03), &stcGpioInit);

    /* PA2 = I_Total (U9 TP181A1 output). */
    (void)GPIO_Init(GPIO_PORT_A, GPIO_PIN_02, &stcGpioInit);

    CLK_SetPeriClockSrc(CLK_PERIPHCLK_PCLK);

    (void)ADC_StructInit(&stcAdcInit);
    (void)ADC_Init(CM_ADC1, &stcAdcInit);

    ADC_ChCmd(CM_ADC1, ADC_SEQ_A, ADC_CH2,  ENABLE);    /* I_total (TP181) */
    ADC_ChCmd(CM_ADC1, ADC_SEQ_A, ADC_CH10, ENABLE);    /* VBUS */
    ADC_ChCmd(CM_ADC1, ADC_SEQ_A, ADC_CH11, ENABLE);    /* IA   */
    ADC_ChCmd(CM_ADC1, ADC_SEQ_A, ADC_CH12, ENABLE);    /* IB   */
    ADC_ChCmd(CM_ADC1, ADC_SEQ_A, ADC_CH13, ENABLE);    /* NTC  */

    ADC_SetSampleTime(CM_ADC1, ADC_CH2,  0x20U);
    ADC_SetSampleTime(CM_ADC1, ADC_CH10, 0x20U);
    ADC_SetSampleTime(CM_ADC1, ADC_CH11, 0x20U);
    ADC_SetSampleTime(CM_ADC1, ADC_CH12, 0x20U);
    ADC_SetSampleTime(CM_ADC1, ADC_CH13, 0x20U);
}

/**
 * @brief  One blocking ADC sequence conversion (only used before the PWM
 *         timer starts, and for the DC_CAL offset measurement).
 */
static uint8_t AdcReadBlocking(uint16_t *pIa, uint16_t *pIb)
{
    uint32_t u32Timeout = 200000UL;

    /* Discard a stale conversion so that the values below are fresh. */
    ADC_ClearStatus(CM_ADC1, ADC_FLAG_EOCA);

    (void)ADC_Start(CM_ADC1);
    while (ADC_GetStatus(CM_ADC1, ADC_FLAG_EOCA) != SET) {
        u32Timeout--;
        if (u32Timeout == 0UL) {
            return 0U;
        }
    }

    *pIa = ADC_GetValue(CM_ADC1, ADC_CH11);
    *pIb = ADC_GetValue(CM_ADC1, ADC_CH12);

    ADC_ClearStatus(CM_ADC1, ADC_FLAG_EOCA);
    return 1U;
}

/**
 * @brief  Blocking ADC read that is safe while the 20 kHz PWM interrupt runs.
 *         The TMRA interrupt is paused so the ISR cannot start a conversion
 *         in the middle of the sequence.
 */
static uint8_t AdcReadBlockingSafe(uint16_t *pIa, uint16_t *pIb)
{
    uint8_t u8Ret;

    if (s_u8PwmReady != 0U) {
        TMRA_IntCmd(CM_TMRA_1, TMRA_INT_OVF, DISABLE);
    }

    u8Ret = AdcReadBlocking(pIa, pIb);

    if (s_u8PwmReady != 0U) {
        s_u8AdcBusy = 0U;       /* force the ISR to start a fresh conversion */
        TMRA_IntCmd(CM_TMRA_1, TMRA_INT_OVF, ENABLE);
    }

    return u8Ret;
}

/**
 * @brief  Measure the shunt amplifier zero-current offset with DC_CAL.
 */
static void AdcCalibrate(void)
{
    uint32_t u32SumA = 0UL;
    uint32_t u32SumB = 0UL;
    uint16_t u16Ia;
    uint16_t u16Ib;
    uint8_t i;
    uint8_t u8Ok = 0U;

    s_fIaOfs = 2048.0f;
    s_fIbOfs = 2048.0f;

    if (s_u8DrvOk == 0U) {
        return;
    }

    DC_CAL_HIGH();
    DDL_DelayMS(5UL);

    for (i = 0U; i < 8U; i++) {
        if (AdcReadBlockingSafe(&u16Ia, &u16Ib) == 0U) {
            DC_CAL_LOW();
            return;
        }
        u32SumA += u16Ia;
        u32SumB += u16Ib;
    }

    DC_CAL_LOW();
    DDL_DelayMS(1UL);

    s_fIaOfs = (float)(u32SumA / 8UL);
    s_fIbOfs = (float)(u32SumB / 8UL);

    u8Ok = 1U;
    if ((s_fIaOfs < 1500.0f) || (s_fIaOfs > 2600.0f)) {
        u8Ok = 0U;
    }
    if ((s_fIbOfs < 1500.0f) || (s_fIbOfs > 2600.0f)) {
        u8Ok = 0U;
    }
    if (u8Ok == 0U) {
        /* Amplifier output is not near VREF/2 - fall back to nominal. */
        s_fIaOfs = 2048.0f;
        s_fIbOfs = 2048.0f;
    }

    printf("Current amp offset: IA=%d IB=%d counts (%s)\r\n",
           (int)s_fIaOfs, (int)s_fIbOfs,
           (u8Ok != 0U) ? "ok" : "nominal");
}

/**
 * @brief  TP181 total-current zero offset.  U9 is supplied from the 3.3 V rail
 *         (independent of EN_GATE), so this runs with the bridge disabled.
 */
static void AdcCalItotal(void)
{
    uint32_t u32Sum = 0UL;
    uint16_t u16Ia;
    uint16_t u16Ib;
    uint8_t i;

    s_fItotalOfs = 2048.0f;

    for (i = 0U; i < 8U; i++) {
        if (AdcReadBlocking(&u16Ia, &u16Ib) == 0U) {
            return;
        }
        u32Sum += ADC_GetValue(CM_ADC1, ADC_CH2);
    }

    s_fItotalOfs = (float)(u32Sum / 8UL);

    if ((s_fItotalOfs < 1500.0f) || (s_fItotalOfs > 2600.0f)) {
        printf("TP181: offset %d out of range, check U9 / 3.3V\r\n", (int)s_fItotalOfs);
    } else {
        printf("TP181: offset %d counts\r\n", (int)s_fItotalOfs);
    }
}

/* ============================== PWM ====================================== */

static float SinTab(float fIdx)
{
    uint32_t u32Idx;
    float fFrac;
    float fA;
    float fB;

    u32Idx = (uint32_t)fIdx;
    fFrac = fIdx - (float)u32Idx;
    u32Idx &= (SIN_TAB_SIZE - 1U);
    fA = s_afSin[u32Idx];
    fB = s_afSin[(u32Idx + 1U) & (SIN_TAB_SIZE - 1U)];

    return fA + ((fB - fA) * fFrac);
}

static void PwmSetDuty(float fA, float fB, float fC)
{
    uint32_t u32A;
    uint32_t u32B;
    uint32_t u32C;

    if (fA < 0.0f) { fA = 0.0f; }
    if (fA > 1.0f) { fA = 1.0f; }
    if (fB < 0.0f) { fB = 0.0f; }
    if (fB > 1.0f) { fB = 1.0f; }
    if (fC < 0.0f) { fC = 0.0f; }
    if (fC > 1.0f) { fC = 1.0f; }

    u32A = (uint32_t)(fA * (float)PWM_PERIOD);
    u32B = (uint32_t)(fB * (float)PWM_PERIOD);
    u32C = (uint32_t)(fC * (float)PWM_PERIOD);

    TMRA_SetCompareValue(CM_TMRA_1, TMRA_CH1, u32A);
    TMRA_SetCompareValue(CM_TMRA_1, TMRA_CH2, u32B);
    TMRA_SetCompareValue(CM_TMRA_1, TMRA_CH3, u32C);
}

/**
 * @brief  20 kHz PWM interrupt.
 *
 * At the triangle top (OVF) the low-side devices are on, so this is where the
 * ADC sequence is started: the shunt currents are valid during this window.
 */
static void TmraOvfIrqCallback(void)
{
    float fIdx;
    float fM;
    float fA;
    float fB;
    float fC;

    TMRA_ClearStatus(CM_TMRA_1, TMRA_FLAG_OVF);

    if ((s_enState == ST_ALIGN) || (s_enState == ST_RAMP) ||
        (s_enState == ST_SPIN) || (s_enState == ST_STOP)) {
        s_fTheta += (TWO_PI_F * s_fFreq) / (float)PWM_FREQ_HZ;
        if (s_fTheta >= TWO_PI_F) {
            s_fTheta -= TWO_PI_F;
        }

        fIdx = s_fTheta * ((float)SIN_TAB_SIZE / TWO_PI_F);
        fM = s_fMod;

        fA = 0.5f + (0.5f * fM * SinTab(fIdx));
        fB = 0.5f + (0.5f * fM * SinTab(fIdx + 85.3333f));
        fC = 0.5f + (0.5f * fM * SinTab(fIdx + 170.6667f));
        PwmSetDuty(fA, fB, fC);
    }

    if (++s_u32PwmCnt >= (PWM_FREQ_HZ / 1000UL)) {
        s_u32PwmCnt = 0UL;
        s_u8Tick1ms = 1U;
    }

    if (s_u8AdcBusy == 0U) {
        s_u8AdcBusy = 1U;
        (void)ADC_Start(CM_ADC1);
    }
}

static void PwmInit(void)
{
    stc_tmra_init_t stcTmraInit;
    stc_tmra_pwm_init_t stcPwmInit;
    stc_irq_signin_config_t stcIrqSigninConfig;
    uint32_t i;

    /* Sine table for the ISR. */
    for (i = 0UL; i < SIN_TAB_SIZE; i++) {
        s_afSin[i] = sinf(TWO_PI_F * (float)i / (float)SIN_TAB_SIZE);
    }

    FCG_Fcg2PeriphClockCmd(FCG2_PERIPH_TMRA_1, ENABLE);

    /* PA8/PA9/PA10 -> TIMA_1_PWM1/2/3, function 4. */
    GPIO_SetFunc(PWM_AH_PORT, PWM_AH_PIN, GPIO_FUNC_4);
    GPIO_SetFunc(PWM_BH_PORT, PWM_BH_PIN, GPIO_FUNC_4);
    GPIO_SetFunc(PWM_CH_PORT, PWM_CH_PIN, GPIO_FUNC_4);

    (void)TMRA_StructInit(&stcTmraInit);
    stcTmraInit.sw_count.u8ClockDiv  = TMRA_CLK_DIV1;
    stcTmraInit.sw_count.u8CountMode = TMRA_MD_TRIANGLE;
    stcTmraInit.sw_count.u8CountDir  = TMRA_DIR_UP;
    stcTmraInit.u32PeriodValue       = PWM_PERIOD;
    (void)TMRA_Init(CM_TMRA_1, &stcTmraInit);

    (void)TMRA_PWM_StructInit(&stcPwmInit);
    stcPwmInit.u32CompareValue        = PWM_PERIOD / 2UL;
    stcPwmInit.u16StartPolarity       = TMRA_PWM_HIGH;   /* duty = CMP / PERIOD */
    stcPwmInit.u16StopPolarity        = TMRA_PWM_LOW;
    stcPwmInit.u16CompareMatchPolarity = TMRA_PWM_INVT;
    stcPwmInit.u16PeriodMatchPolarity = TMRA_PWM_HOLD;

    (void)TMRA_PWM_Init(CM_TMRA_1, TMRA_CH1, &stcPwmInit);
    (void)TMRA_PWM_Init(CM_TMRA_1, TMRA_CH2, &stcPwmInit);
    (void)TMRA_PWM_Init(CM_TMRA_1, TMRA_CH3, &stcPwmInit);

    TMRA_PWM_OutputCmd(CM_TMRA_1, TMRA_CH1, ENABLE);
    TMRA_PWM_OutputCmd(CM_TMRA_1, TMRA_CH2, ENABLE);
    TMRA_PWM_OutputCmd(CM_TMRA_1, TMRA_CH3, ENABLE);

    /* 20 kHz period interrupt = ADC trigger + 1 ms time base. */
    stcIrqSigninConfig.enIRQn = INT080_IRQn;
    stcIrqSigninConfig.enIntSrc = INT_SRC_TMRA_1_OVF;
    stcIrqSigninConfig.pfnCallback = &TmraOvfIrqCallback;
    INTC_IrqInstalHandler(&stcIrqSigninConfig, DDL_IRQ_PRIO_03);

    TMRA_IntCmd(CM_TMRA_1, TMRA_INT_OVF, ENABLE);

    TMRA_SetCountValue(CM_TMRA_1, 1UL);
    TMRA_Start(CM_TMRA_1);

    s_u8PwmReady = 1U;
}

/* ============================ measurements ============================== */

static uint8_t HallRead(void)
{
    uint8_t u8Hall = 0U;

    if (GPIO_ReadInputPins(HALL_A_PORT, HALL_A_PIN) == PIN_SET) { u8Hall |= 0x01U; }
    if (GPIO_ReadInputPins(HALL_B_PORT, HALL_B_PIN) == PIN_SET) { u8Hall |= 0x02U; }
    if (GPIO_ReadInputPins(HALL_C_PORT, HALL_C_PIN) == PIN_SET) { u8Hall |= 0x04U; }

    return u8Hall;
}

static void UpdateMeasurements(void)
{
    float fVolts;
    float fIa;
    float fIb;
    float fItotal;
    float fRntc;

    s_u16RawVbus = ADC_GetValue(CM_ADC1, ADC_CH10);
    s_u16RawIa   = ADC_GetValue(CM_ADC1, ADC_CH11);
    s_u16RawIb   = ADC_GetValue(CM_ADC1, ADC_CH12);
    s_u16RawNtc  = ADC_GetValue(CM_ADC1, ADC_CH13);
    s_u16RawItotal = ADC_GetValue(CM_ADC1, ADC_CH2);

    s_fVbus = ((float)s_u16RawVbus * ADC_VREF / ADC_FULL_SCALE) / VBUS_DIV;

    fIa = ((float)s_u16RawIa - s_fIaOfs) * (ADC_VREF / ADC_FULL_SCALE) / (SHUNT_OHM * AMP_GAIN);
    fIb = ((float)s_u16RawIb - s_fIbOfs) * (ADC_VREF / ADC_FULL_SCALE) / (SHUNT_OHM * AMP_GAIN);

    /* Light low-pass on the 20 kHz samples. */
    s_fIa += 0.10f * (fIa - s_fIa);
    s_fIb += 0.10f * (fIb - s_fIb);

    s_fIamp = (fabsf(s_fIa) > fabsf(s_fIb)) ? fabsf(s_fIa) : fabsf(s_fIb);

    /* TP181: Vout = 1.65 V - 0.25 V/A * I, so the sign is inverted. */
    fItotal = -(((float)s_u16RawItotal - s_fItotalOfs) * (ADC_VREF / ADC_FULL_SCALE) /
                (ITOTAL_SHUNT_OHM * ITOTAL_GAIN));
    s_fItotal += 0.10f * (fItotal - s_fItotal);

    /* NTC: V = 3.3 * Rntc / (Rntc + 4.7k) */
    fVolts = (float)s_u16RawNtc * ADC_VREF / ADC_FULL_SCALE;
    if ((fVolts > 0.10f) && (fVolts < 3.20f)) {
        fRntc = NTC_PULLUP_OHM * fVolts / (ADC_VREF - fVolts);
        s_fTempC = (1.0f / ((1.0f / 298.15f) + (logf(fRntc / NTC_R25_OHM) / NTC_BETA))) - 273.15f;
        s_u8TempValid = 1U;
    } else {
        s_u8TempValid = 0U;
    }

    /* Hard over-current trip (also while ramping down: regeneration). */
    if ((s_enState == ST_ALIGN) || (s_enState == ST_RAMP) ||
        (s_enState == ST_SPIN) || (s_enState == ST_STOP)) {
        if (s_fIamp > I_TRIP_A) {
            s_u8OcCnt++;
            if (s_u8OcCnt >= I_TRIP_CNT) {
                EnterFault("OVERCURRENT");
            }
        } else {
            s_u8OcCnt = 0U;
        }
    } else {
        s_u8OcCnt = 0U;
    }

    /* Independent total-current trip (TP181).  Coarse backup: at partial
       modulation the bus current is lower than the phase current. */
    if ((s_enState == ST_ALIGN) || (s_enState == ST_RAMP) ||
        (s_enState == ST_SPIN) || (s_enState == ST_STOP)) {
        if (fabsf(s_fItotal) > ITOTAL_TRIP_A) {
            s_u8ItotalTripCnt++;
            if (s_u8ItotalTripCnt >= ITOTAL_TRIP_CNT) {
                EnterFault("OVERCURRENT(BUS)");
            }
        } else {
            s_u8ItotalTripCnt = 0U;
        }
    } else {
        s_u8ItotalTripCnt = 0U;
    }
}

/* ============================ state machine ============================== */

static void EnterFault(const char *pszReason)
{
    /* Read the status while the DRV8301 is still powered (EN_GATE high);
       a full EN_GATE low resets the SPI registers and powers the blocks down. */
    s_u16DrvSr1 = Drv8301Read(DRV_REG_SR1);
    s_u16DrvSr2 = Drv8301Read(DRV_REG_SR2);

    EN_GATE_LOW();              /* hardware shutdown first: all gates off */
    s_u8DrvOk = 0U;
    s_enState = ST_FAULT;
    s_fMod = 0.0f;
    s_fFreq = 0.0f;
    s_fModScale = 1.0f;
    s_u8OcCnt = 0U;
    PwmSetDuty(0.5f, 0.5f, 0.5f);

    printf("\r\n*** FAULT: %s ***\r\n", pszReason);
    Drv8301PrintFaults(s_u16DrvSr1, s_u16DrvSr2);
    printf("Press 'r' to reset the DRV8301, then 's' to retry.\r\n");
}

static void MotorStart(void)
{
    uint16_t u16Sr1;

    /* A start takes over EN_GATE from the 'e' diagnostic hold. */
    s_u8EnGateHold = 0U;

    if (s_fVbus < VBUS_MIN_START) {
        printf("Vbus too low (");
        PrintF1(s_fVbus);
        printf(" V). Need >= ");
        PrintF1(VBUS_MIN_START);
        printf(" V.\r\n");
        return;
    }
    if (s_fVbus > VBUS_MAX_RUN) {
        printf("Vbus too high, refusing to start.\r\n");
        return;
    }

    /* Zero differential voltage before the gate driver is enabled. */
    s_fFreq = 0.0f;
    s_fMod = 0.0f;
    s_fModScale = 1.0f;
    s_fTheta = 0.0f;
    s_u32AlignMs = 0UL;
    s_u8OcCnt = 0U;
    s_u8ItotalTripCnt = 0U;
    s_fIa = 0.0f;
    s_fIb = 0.0f;
    s_fIamp = 0.0f;
    s_fItotal = 0.0f;
    PwmSetDuty(0.5f, 0.5f, 0.5f);

    /* EN_GATE was low while idle, so the DRV8301 is in its power-on default
       state: reconfigure and verify the registers on every start. */
    (void)Drv8301SpiWake();
    Drv8301Init(0U);

    if (s_u8DrvOk == 0U) {
        EN_GATE_LOW();
        printf("Start aborted: DRV8301 SPI not responding.\r\n");
        return;
    }

    u16Sr1 = Drv8301Read(DRV_REG_SR1);
    s_u16DrvSr1 = u16Sr1;
    s_u16DrvSr2 = Drv8301Read(DRV_REG_SR2);

    if (((u16Sr1 & DRV_SR1_FAULT) != 0U) || NFAULT_LOW()) {
        EN_GATE_LOW();
        s_u8DrvOk = 0U;
        printf("DRV8301 reports a fault, not starting:\r\n");
        Drv8301PrintFaults(s_u16DrvSr1, s_u16DrvSr2);
        printf("Press 'r' to clear the fault.\r\n");
        return;
    }

    s_enState = ST_ALIGN;
    printf("Start: align rotor, then V/f ramp (f=");
    PrintF1(s_fFreqCmd);
    printf(" Hz, mod=");
    PrintF2(s_fModCmd);
    printf(")\r\n");
}

static void MotorStop(void)
{
    if ((s_enState == ST_IDLE) || (s_enState == ST_FAULT)) {
        return;
    }
    s_enState = ST_STOP;
    printf("Stop: ramping down...\r\n");
}

static void ControlTick(void)
{
    float fTargetF;
    float fTargetM;
    float fStepF;
    float fStepM;
    uint8_t u8Hall;
    uint8_t u8StartKey;

    s_u32Ms++;

    /* ---- ADC liveness: without current samples there is no protection ---- */
    if (s_u16AdcAliveMs < 1000U) {
        s_u16AdcAliveMs++;
    }
    if (((s_enState == ST_ALIGN) || (s_enState == ST_RAMP) || (s_enState == ST_SPIN)) &&
        (s_u16AdcAliveMs > 100U)) {
        EnterFault("ADC no data");
        return;
    }

    /* ---- current foldback on the modulation index ---- */
    if ((s_enState == ST_ALIGN) || (s_enState == ST_RAMP) || (s_enState == ST_SPIN)) {
        if (s_fIamp > I_SOFT_LIMIT_A) {
            s_fModScale -= 0.002f;              /* 0.2 % per ms */
            if (s_fModScale < 0.05f) {
                s_fModScale = 0.05f;
            }
        } else if (s_fIamp < (I_SOFT_LIMIT_A * 0.7f)) {
            s_fModScale += 0.0005f;
            if (s_fModScale > 1.0f) {
                s_fModScale = 1.0f;
            }
        }
    }

    /* ---- state machine / slew-rate ramp ---- */
    fTargetF = 0.0f;
    fTargetM = 0.0f;

    switch (s_enState) {
        case ST_ALIGN:
            fTargetF = 0.0f;
            fTargetM = s_fModCmd * s_fModScale;
            if (++s_u32AlignMs >= ALIGN_TIME_MS) {
                s_enState = ST_RAMP;
            }
            break;

        case ST_RAMP:
            fTargetF = s_fFreqCmd;
            fTargetM = s_fModCmd * s_fModScale;
            if (fabsf(s_fFreq - s_fFreqCmd) < 0.05f) {
                s_enState = ST_SPIN;
            }
            break;

        case ST_SPIN:
            fTargetF = s_fFreqCmd;
            fTargetM = s_fModCmd * s_fModScale;
            break;

        case ST_STOP:
            fTargetF = 0.0f;
            fTargetM = 0.0f;
            break;

        case ST_FAULT:
        case ST_IDLE:
        default:
            fTargetF = 0.0f;
            fTargetM = 0.0f;
            break;
    }

    fStepF = ((s_enState == ST_STOP) ? FREQ_SLEW_DN : FREQ_SLEW_UP) * 0.001f;
    fStepM = ((s_enState == ST_STOP) ? MOD_SLEW_DN : MOD_SLEW_UP) * 0.001f;

    if (s_fFreq < fTargetF) {
        s_fFreq += fStepF;
        if (s_fFreq > fTargetF) { s_fFreq = fTargetF; }
    } else {
        s_fFreq -= fStepF;
        if (s_fFreq < fTargetF) { s_fFreq = fTargetF; }
    }

    if (s_fMod < fTargetM) {
        s_fMod += fStepM;
        if (s_fMod > fTargetM) { s_fMod = fTargetM; }
    } else {
        s_fMod -= fStepM;
        if (s_fMod < fTargetM) { s_fMod = fTargetM; }
    }

    if ((s_enState == ST_STOP) && (s_fMod < 0.002f) && (s_fFreq < 0.1f)) {
        EN_GATE_LOW();
        s_u8DrvOk = 0U;
        PwmSetDuty(0.5f, 0.5f, 0.5f);
        s_enState = ST_IDLE;
        printf("Stopped.\r\n");
    }

    /* ---- nFAULT monitor ---- */
    if ((s_enState == ST_ALIGN) || (s_enState == ST_RAMP) ||
        (s_enState == ST_SPIN) || (s_enState == ST_STOP)) {
        if (NFAULT_LOW()) {
            EnterFault("nFAULT pin");
        }
    }

    /* ---- keys ----
       Prefixed with ">>" plus a blank line, so the event is not buried
       between the periodic status lines. */
    if (KeyPressed(KEY1_PORT, KEY1_PIN, &s_u8Key1Stable, &s_u8Key1Cnt) != 0U) {
        u8StartKey = (uint8_t)(((s_enState == ST_IDLE) || (s_enState == ST_FAULT)) ? 1U : 0U);
        printf("\r\n>> KEY1: %s\r\n", (u8StartKey != 0U) ? "start" : "stop");
        if (u8StartKey != 0U) {
            MotorStart();
        } else {
            MotorStop();
        }
    }
    if (KeyPressed(KEY2_PORT, KEY2_PIN, &s_u8Key2Stable, &s_u8Key2Cnt) != 0U) {
        s_fFreqCmd += 1.0f;
        if (s_fFreqCmd > FREQ_MAX_HZ) { s_fFreqCmd = FREQ_MAX_HZ; }
        printf("\r\n>> KEY2: f = ");
        PrintF1(s_fFreqCmd);
        printf(" Hz\r\n");
    }
    if (KeyPressed(KEY3_PORT, KEY3_PIN, &s_u8Key3Stable, &s_u8Key3Cnt) != 0U) {
        s_fFreqCmd -= 1.0f;
        if (s_fFreqCmd < FREQ_MIN_HZ) { s_fFreqCmd = FREQ_MIN_HZ; }
        printf("\r\n>> KEY3: f = ");
        PrintF1(s_fFreqCmd);
        printf(" Hz\r\n");
    }

    /* ---- LED ---- */
    if (s_enState == ST_FAULT) {
        u8Hall = (uint8_t)((s_u32Ms % 100UL) < 50UL);
    } else if ((s_enState == ST_ALIGN) || (s_enState == ST_RAMP) || (s_enState == ST_SPIN)) {
        u8Hall = 1U;
    } else {
        u8Hall = (uint8_t)((s_u32Ms % 1000UL) < 500UL);
    }
    if (u8Hall != 0U) {
        if (s_u8LedOn == 0U) { LED_ON(); s_u8LedOn = 1U; }
    } else {
        if (s_u8LedOn != 0U) { LED_OFF(); s_u8LedOn = 0U; }
    }

    /* ---- monitor line, 1 Hz ---- */
    if ((s_u8Monitor != 0U) && ((s_u32Ms % MONITOR_PERIOD_MS) == 0UL)) {
        PrintStatus();
    }
}

/* ============================== console ================================= */

static void PrintF1(float fVal)
{
    int32_t i32Val;

    i32Val = (int32_t)((fVal * 10.0f) + ((fVal >= 0.0f) ? 0.5f : -0.5f));
    if (i32Val < 0) {
        printf("-");
        i32Val = -i32Val;
    }
    printf("%d.%d", (int)(i32Val / 10), (int)(i32Val % 10));
}

static void PrintF2(float fVal)
{
    int32_t i32Val;

    i32Val = (int32_t)((fVal * 100.0f) + ((fVal >= 0.0f) ? 0.5f : -0.5f));
    if (i32Val < 0) {
        printf("-");
        i32Val = -i32Val;
    }
    printf("%d.%02d", (int)(i32Val / 100), (int)(i32Val % 100));
}

static uint8_t KeyPressed(uint8_t u8Port, uint16_t u16Pin, uint8_t *pu8Stable, uint8_t *pu8Cnt)
{
    uint8_t u8Raw;

    u8Raw = (GPIO_ReadInputPins(u8Port, u16Pin) == PIN_RESET) ? 1U : 0U;

    if (u8Raw != *pu8Stable) {
        (*pu8Cnt)++;
        if (*pu8Cnt >= 20U) {
            *pu8Stable = u8Raw;
            *pu8Cnt = 0U;
            return u8Raw;           /* 1 only on the press edge */
        }
    } else {
        *pu8Cnt = 0U;
    }

    return 0U;
}

static void PrintHelp(void)
{
    printf("\r\n--- commands ---\r\n");
    printf(" s : start / stop\r\n");
    printf(" + - : electrical frequency +/- 1 Hz\r\n");
    printf(" a z : modulation index +/- 0.02\r\n");
    printf(" d : DRV8301 registers / fault status\r\n");
    printf(" e : hold EN_GATE high (measure U1 rails), 'e' again to release\r\n");
    printf(" t : slow SPI scan (20 kHz)\r\n");
    printf(" r : clear DRV8301 faults and re-init\r\n");
    printf(" m : 1 Hz status line on/off\r\n");
    printf(" h : print one status line\r\n");
    printf(" ? : this help\r\n");
}

static void PrintStatus(void)
{
    const char *pszState;

    switch (s_enState) {
        case ST_IDLE:  pszState = "IDLE";  break;
        case ST_ALIGN: pszState = "ALIGN"; break;
        case ST_RAMP:  pszState = "RAMP";  break;
        case ST_SPIN:  pszState = "SPIN";  break;
        case ST_STOP:  pszState = "STOP";  break;
        case ST_FAULT: pszState = "FAULT"; break;
        default:       pszState = "?";     break;
    }

    printf("[%s] f=", pszState);
    PrintF1(s_fFreq);
    printf("Hz m=");
    PrintF2(s_fMod);
    printf(" V=");
    PrintF1(s_fVbus);
    printf("V I=");
    if ((s_enState == ST_IDLE) || (s_enState == ST_FAULT)) {
        printf("--");           /* current amplifiers are off while EN_GATE is low */
    } else {
        PrintF2(s_fIamp);
    }
    printf("A It=");
    PrintF2(s_fItotal);
    printf("A T=");
    if (s_u8TempValid != 0U) {
        PrintF1(s_fTempC);
    } else {
        printf("--");
    }
    printf("C hall=%u drv=%s\r\n", (unsigned)HallRead(), (s_u8DrvOk != 0U) ? "OK" : "off");
}

static void ConsolePoll(void)
{
    uint8_t u8Ch;
    uint8_t u8Raised;
    float fVal;

    while (0UL != UART_ReadByte(&u8Ch)) {
        switch (u8Ch) {
            case '?':
                PrintHelp();
                break;

            case 's':
                if ((s_enState == ST_IDLE) || (s_enState == ST_FAULT)) {
                    MotorStart();
                } else {
                    MotorStop();
                }
                break;

            case '+':
                s_fFreqCmd += 1.0f;
                if (s_fFreqCmd > FREQ_MAX_HZ) { s_fFreqCmd = FREQ_MAX_HZ; }
                printf("f = ");
                PrintF1(s_fFreqCmd);
                printf(" Hz\r\n");
                break;

            case '-':
                s_fFreqCmd -= 1.0f;
                if (s_fFreqCmd < FREQ_MIN_HZ) { s_fFreqCmd = FREQ_MIN_HZ; }
                printf("f = ");
                PrintF1(s_fFreqCmd);
                printf(" Hz\r\n");
                break;

            case 'a':
                fVal = s_fModCmd + 0.02f;
                if (fVal > MOD_MAX) { fVal = MOD_MAX; }
                s_fModCmd = fVal;
                printf("mod = ");
                PrintF2(s_fModCmd);
                printf("\r\n");
                break;

            case 'z':
                fVal = s_fModCmd - 0.02f;
                if (fVal < MOD_MIN) { fVal = MOD_MIN; }
                s_fModCmd = fVal;
                printf("mod = ");
                PrintF2(s_fModCmd);
                printf("\r\n");
                break;

            case 'd':
                Drv8301Dump();
                break;

            case 'e':
                EnGateHoldToggle();
                break;

            case 't':
                Drv8301SpiScan();
                break;

            case 'r':
                if ((s_enState != ST_IDLE) && (s_enState != ST_FAULT)) {
                    printf("Stop the motor first ('s').\r\n");
                    break;
                }
                u8Raised = Drv8301SpiWake();
                Drv8301ClearFaults();
                DDL_DelayMS(2UL);
                Drv8301Init(1U);
                if (s_u8DrvOk != 0U) {
                    AdcCalibrate();
                }
                if ((u8Raised != 0U) && (s_u8EnGateHold == 0U)) {
                    EN_GATE_LOW();
                    s_u8DrvOk = 0U;
                }
                s_enState = ST_IDLE;
                printf("DRV8301 reset done.\r\n");
                break;

            case 'm':
                s_u8Monitor = (uint8_t)((s_u8Monitor != 0U) ? 0U : 1U);
                printf("monitor %s\r\n", (s_u8Monitor != 0U) ? "on" : "off");
                break;

            case 'h':
                PrintStatus();
                Drv8301Dump();
                break;

            default:
                break;
        }
    }
}

/* =============================== main =================================== */

int32_t main(void)
{
    uint16_t u16Ia;
    uint16_t u16Ib;
    uint8_t u8Hall;

    LL_PERIPH_WE(LL_PERIPH_SEL);

    SystemClockInit();
    UART_Init();
    GpioInit();
    Spi2Init(SPI_BR_FAST);
    AdcInit();

    /* TP181 offset: U9 is on the 3.3 V rail, gates are still off. */
    DDL_DelayMS(5UL);
    AdcCalItotal();

    /* Start the 20 kHz timer first: all three phases at 50 % (zero
       differential voltage), EN_GATE still low so the gates are off. */
    PwmInit();

    /* DRV8301 is powered from PVDD; EN_GATE must be high for the SPI, the
       current shunt amplifiers and the gate driver (tSPI_READY = 10 ms). */
    EN_GATE_HIGH();
    DDL_DelayMS(20UL);

    Drv8301Init(1U);
    s_u8DrvBootOk = s_u8DrvOk;

    if (s_u8DrvOk != 0U) {
        AdcCalibrate();
    }

    /* First measurements for the banner. */
    if (AdcReadBlockingSafe(&u16Ia, &u16Ib) != 0U) {
        s_u16RawVbus = ADC_GetValue(CM_ADC1, ADC_CH10);
        s_u16RawNtc  = ADC_GetValue(CM_ADC1, ADC_CH13);
        s_fVbus = ((float)s_u16RawVbus * ADC_VREF / ADC_FULL_SCALE) / VBUS_DIV;
    }
    u8Hall = HallRead();

    /* Idle: gates off.  A full EN_GATE low also resets the DRV8301 registers,
       so MotorStart() reconfigures the device every time. */
    EN_GATE_LOW();
    s_u8DrvOk = 0U;

    LL_PERIPH_WP(LL_PERIPH_EFM | LL_PERIPH_GPIO | LL_PERIPH_PWC_CLK_RMU | LL_PERIPH_SRAM);

    printf("\r\n========================================\r\n");
    printf(" HC32F460KETA + DRV8301 bring-up test\r\n");
    printf(" 20 kHz 3-PWM, open-loop V/f (no FOC)\r\n");
    printf("========================================\r\n");
    printf("DRV8301: %s\r\n", (s_u8DrvBootOk != 0U) ? "configured (now in reset)" : "NOT RESPONDING");
    printf("Vbus=");
    PrintF1(s_fVbus);
    printf(" V  hall=%u  nFAULT=%u\r\n",
           (unsigned)u8Hall, (unsigned)(NFAULT_LOW() ? 0U : 1U));
    printf("Default: f=");
    PrintF1(s_fFreqCmd);
    printf(" Hz, mod=");
    PrintF2(s_fModCmd);
    printf(". Type '?' for help.\r\n");

    for (;;) {
        /* ADC results: started by the PWM ISR at the triangle top. */
        if (ADC_GetStatus(CM_ADC1, ADC_FLAG_EOCA) == SET) {
            ADC_ClearStatus(CM_ADC1, ADC_FLAG_EOCA);
            s_u16AdcAliveMs = 0U;
            UpdateMeasurements();
            s_u8AdcBusy = 0U;
        }

        if (s_u8Tick1ms != 0U) {
            s_u8Tick1ms = 0U;
            ControlTick();
        }

        ConsolePoll();
    }
}

/*******************************************************************************
 * EOF (not truncated)
 ******************************************************************************/
