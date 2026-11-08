/**
 *******************************************************************************
 * @file  foc/source/board_config.c
 * @brief hard_init() - HC32F460KETA + DRV8301 ���������弶Ӳ����ʼ��
 *
 * ��ֲ�� STM32F4 ���׹���:
 *   ���״���/����/code/user/board_config.c  (hard_init)
 *
 * Դ���̸����ܿ��뱾��ֲ�Ķ�Ӧ��ϵ (hard_init �ڲ�˳��һ��):
 *   Դ���� (STM32F446)                     ����ֲ (HC32F460)
 *   -------------------------              ---------------------------
 *   RCC GPIO/TIM/ADC/DMA ʱ��ʹ��           FCG ������ֱ��ſ�
 *   SysTick_Config(HCLK/1000)              TMRA_1 ����жϲ��� 1ms ��־
 *   TIM2 �����ӿ� (TI1 ˫����+��ģʽ��λ)    TMRA_2 ��ͨ�� (CH1/CH2/CH4 ��Ӧ
 *                                          PA0/PA1/PA3) Ӳ��˫�ز��� + ���
 *                                          ������չ + ��������; ��������
 *                                          GPIO ����ƽ (Դ����ͬ�����,
 *                                          ��Դ hall_sensor.c TIM2_IRQHandler)
 *   DRV8301 ���� GPIO ��ʼ��ƽ              ͬ������, ͬ���Ŀ��е�ƽ
 *   OLED SPI ����                          �����޶�ӦӲ�� (OLED ����Ϊ
 *                                          I2C PC7/PC8, ��������ֲ)
 *   USER LED PC9                           PC9, �͵�ƽ����
 *   TIM1 6 · PWM + BDTR ����              TMRA_1 3 · PWM (DRV8301 �ڲ�
 *                                          ��������), 20kHz
 *   ADC1 ע���� (T1_CC4 ����) + DMA ������   ADC1 ���� A, �� 20kHz PWM �ж�
 *                                          �����ǲ�������������
 *   ���� EXTI �½��� + NVIC ����            ͬΪ EXTI �½��� (EIRQ2/3/4),
 *                                          �� "���� + 1ms ��ʱ����װ" ����
 *   USB PC ͨ�� (pc_communication_init)     usb_dev_init() �� main �����
 *                                          (usb_bsp.c/cdc_data_process.c,
 *                                          ��ֲ�� usb_dev_cdc ����)
 *   ADC JEOC �ж�                          ��Ϊ��ѯ ADC EOA
 *
 * ��Դ���̵�һ����ȫ��ƫ��: hard_init() ����ʱ EN_GATE ���ֵ͵�ƽ (����
 * �ż��ض�)��Դ���ڴ˴�ֱ������; ���� (�� drv8301_test ��ͬ) �涨: ֻ����
 * DRV8301 �� SPI �Ĵ��������ò�У��ͨ��֮�� (�ɴ���ֲ�� drv8301_init()
 * ���) ������ʹ���ż�������
 *******************************************************************************
 */

#include "board_config.h"
#include "pc_communication.h"   /* communication_task(): PC host link sampling */

/*******************************************************************************
 * �ֲ�����
 ******************************************************************************/
/* TMRA_1 ����ж�����λ, ��ѭ�������ѡ� */
volatile uint8_t g_u8AdcBusy = 0U;
volatile uint8_t g_u8Tick1ms = 0U;

/* TMRA_2 ��������ά�� (��ӦԴ���� hall_sensor.c ��������)�� */
volatile uint32_t g_u32HallEdgeInterval = 0U;   /* 0 = ������Ч��� */
volatile uint32_t g_u32HallEdgeCnt = 0U;
volatile uint8_t  g_u8HallSector = 0U;

/* ������־ (��ӦԴ���� key1_flag/key2_flag/key3_flag)�� */
volatile uint8_t g_u8Key1Flag = 0U;
volatile uint8_t g_u8Key2Flag = 0U;
volatile uint8_t g_u8Key3Flag = 0U;

static uint32_t s_u32PwmCnt = 0UL;      /* 20kHz ��Ƶ�� 1ms �ļ����� */

/* ���������ڲ�״̬ */
static uint32_t s_u32HallOvfCnt = 0UL;  /* ������λ: 16 λ������� */
static uint32_t s_u32HallLastEdge = 0UL;/* ��һ�ص�������չʱ��� */
static uint8_t  s_u8HallEdgeValid = 0U; /* 0: ��һ��ֻ��ʱ���, ������ */

/* ��������: �� 0 ��ʾ�������ڽ����� (���� EXTI ������), ��ѭ�� 1ms �����ʱ */
static volatile uint16_t s_u16Key1DbCnt = 0U;
static volatile uint16_t s_u16Key2DbCnt = 0U;
static volatile uint16_t s_u16Key3DbCnt = 0U;

/* ���벶�������ͨ�� (CH3 �� PA2 ��, �ѱ� TP181 ռ��, ������) */
static const uint32_t s_au32HallCh[3] = { HALL_CAPT_CH_A, HALL_CAPT_CH_B, HALL_CAPT_CH_C };

/*******************************************************************************
 * �ֲ��������� ('static')
 ******************************************************************************/
static void SystemClockInit(void);
static void GpioInit(void);
static void Spi2Init(uint32_t u32Baud);
static uint16_t Spi2Transfer16(uint16_t u16Tx);
static void AdcInit(void);
static void PwmInit(void);
static void Tmra2CaptInit(void);
static void KeyExtiInit(void);
static void Tmra1OvfIrqCallback(void);
static void Tmra2OvfIrqCallback(void);
static void Tmra2CaptIrqCallback(void);
static void Key1IrqCallback(void);
static void Key2IrqCallback(void);
static void Key3IrqCallback(void);

/*******************************************************************************
 * ����ʵ��
 ******************************************************************************/

/**
 * @brief  ϵͳʱ��: �ⲿ���� 16MHz -> MPLL 200MHz + PLLX 48MHz (USB ר��),
 *         PCLK1 100MHz��
 *         (Դ����: RCC_GetClocksFreq + 180MHz PLL, 8M ����;
 *         ���徧�� 16MHz, 200MHz ʱ�������ڰ�����֤;
 *         PLLX 48MHz ���� usb_dev_cdc ����, USB FS �ں�Ҫ��ȷ 48MHz,
 *         ���� PLL �໥������)
 */
static void SystemClockInit(void)
{
    stc_clock_xtal_init_t stcXtalInit;
    stc_clock_pll_init_t  stcMpllInit;
    stc_clock_pllx_init_t stcUpllInit;
    uint32_t u32Wait;

    /* ���߷�Ƶ: HCLK=200MHz, PCLK1=100MHz, PCLK2=50MHz */
    CLK_SetClockDiv(CLK_BUS_CLK_ALL, (CLK_HCLK_DIV1 | CLK_EXCLK_DIV2 | CLK_PCLK0_DIV1 | \
                                      CLK_PCLK1_DIV2 | CLK_PCLK2_DIV4 | CLK_PCLK3_DIV4 | CLK_PCLK4_DIV2));

    /* ����������ģ�⹦��, ʹ�� 16MHz ���� */
    GPIO_AnalogCmd(XTAL_PORT, XTAL_PIN, ENABLE);
    (void)CLK_XtalStructInit(&stcXtalInit);
    stcXtalInit.u8State      = CLK_XTAL_ON;
    stcXtalInit.u8Mode       = CLK_XTAL_MD_OSC;
    stcXtalInit.u8Drv        = CLK_XTAL_DRV_MID;    /* 16MHz ���� [16,20) ����, �е����� */
    stcXtalInit.u8StableTime = CLK_XTAL_STB_2MS;
    (void)CLK_XtalInit(&stcXtalInit);

    /* �ȴ���������, ��ʱ����ѭ�� (������ LED, ��Դ����һ��, ���������) */
    u32Wait = 100UL;
    while ((SET != CLK_GetStableStatus(CLK_STB_FLAG_XTAL)) && (u32Wait > 0UL)) {
        DDL_DelayMS(1UL);
        u32Wait--;
    }
    if (SET != CLK_GetStableStatus(CLK_STB_FLAG_XTAL)) {
        for (;;) {
        }
    }

    /* MPLL ����: 16MHz / 2 * 50 / 2 = 200MHz (VCO = 400MHz) */
    (void)CLK_PLLStructInit(&stcMpllInit);
    stcMpllInit.PLLCFGR          = 0UL;
    stcMpllInit.PLLCFGR_f.PLLM   = (2UL - 1UL);     /* /2          */
    stcMpllInit.PLLCFGR_f.PLLN   = (50UL - 1UL);    /* *50, VCO 400MHz */
    stcMpllInit.PLLCFGR_f.PLLP   = (2UL - 1UL);     /* /2 -> 200MHz    */
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

    /* PLLX (UPLL): USB FS ר��ʱ�� 16MHz / 4 * 84 / 7 = 48MHz��
       �� PLL �� 200MHz / PCLK1 100MHz ���ò���Ӱ�졣 */
    (void)CLK_PLLxStructInit(&stcUpllInit);
    stcUpllInit.PLLCFGR        = 0UL;
    stcUpllInit.PLLCFGR_f.PLLM = (USB_PLL_M - 1UL);
    stcUpllInit.PLLCFGR_f.PLLN = (USB_PLL_N - 1UL);
    stcUpllInit.PLLCFGR_f.PLLP = (USB_PLL_P - 1UL);
    stcUpllInit.PLLCFGR_f.PLLQ = (USB_PLL_P - 1UL);
    stcUpllInit.PLLCFGR_f.PLLR = (USB_PLL_P - 1UL);
    stcUpllInit.u8PLLState     = CLK_PLLX_ON;
    (void)CLK_PLLxInit(&stcUpllInit);

    u32Wait = 100UL;
    while ((SET != CLK_GetStableStatus(CLK_STB_FLAG_PLLX)) && (u32Wait > 0UL)) {
        DDL_DelayMS(1UL);
        u32Wait--;
    }
    if (SET != CLK_GetStableStatus(CLK_STB_FLAG_PLLX)) {
        for (;;) {                  /* PLLX δ����, USB �޷�ö�� */
        }
    }

    /* �л��� PLL ǰ, ������Ƶ������ SRAM / Flash / GPIO �ȴ����� */
    SRAM_SetWaitCycle(SRAM_SRAMH, SRAM_WAIT_CYCLE0, SRAM_WAIT_CYCLE0);
    SRAM_SetWaitCycle((SRAM_SRAM12 | SRAM_SRAM3 | SRAM_SRAMR), SRAM_WAIT_CYCLE1, SRAM_WAIT_CYCLE1);
    (void)EFM_SetWaitCycle(EFM_WAIT_CYCLE5);        /* 126~200MHz: 5 ���ȴ����� */
    GPIO_SetReadWaitCycle(GPIO_RD_WAIT3);
    (void)PWC_HighSpeedToHighPerformance();
    CLK_SetSysClockSrc(CLK_SYSCLK_SRC_PLL);         /* ϵͳʱ��Դ�е� MPLL */

    /* ��λ��ʹ�� Cache */
    EFM_CacheRamReset(ENABLE);
    EFM_CacheRamReset(DISABLE);
    EFM_CacheCmd(ENABLE);
}

/**
 * @brief  ȫ���弶 GPIO����ӦԴ hard_init �еĸ� GPIO ���ÿ�
 *         (DRV8301 / ���� / ���� / LED / PWM ����)��
 *         USB DP/DM (PA11/PA12) �� usb_bsp.c ����Ϊģ���, ���ﲻ����
 */
static void GpioInit(void)
{
    stc_gpio_init_t stcGpioInit;

    /*
     * PA13/PA14/PA15/PB3/PB4 ��λ��Ϊ JTAG/SWD ����, �� PSPCR.SPFE[4:0] =
     * 0x1F��SPFE �����ȼ����� PFSR.FSEL, ����ֻ�� GPIO_Init() ����Щ����
     * �ĺ�����ȫ����Ч (�ֲ� RM 9.4.7 / 9.4.12)��
     * ����: PA15 = nSCS, PB3/PB4 = KEY2/KEY3 -> �ͷ� JTAG ������ SWD
     * (SPFE[0]=JTCK_SWCLK, SPFE[1]=JTMS_SWDIO)��
     * ©����һ�������� drv8301 ��ÿ�� SPI ��ȡ������ FFFF��
     * (Դ�� nSCS Ҳ�� PA15, �� STM32 ��λ������Ĭ�Ͼ�����ͨ IO��)
     */
    GPIO_SetDebugPort((uint8_t)(GPIO_PIN_TDI | GPIO_PIN_TDO | GPIO_PIN_TRST), DISABLE);

    /* ---- �������: EN_GATE ��, DC_CAL ��, LED Ϩ��, SPI ���е�ƽ ---- */
    (void)GPIO_StructInit(&stcGpioInit);
    stcGpioInit.u16PinDir        = PIN_DIR_OUT;
    stcGpioInit.u16PinOutputType = PIN_OUT_TYPE_CMOS;
    stcGpioInit.u16PinDrv        = PIN_LOW_DRV;
    stcGpioInit.u16PinState      = PIN_RESET;
    stcGpioInit.u16PullUp        = PIN_PU_OFF;
    stcGpioInit.u16PinAttr       = PIN_ATTR_DIGITAL;

    (void)GPIO_Init(DRV8301_ENGATE_PORT, DRV8301_ENGATE_PIN, &stcGpioInit);
    (void)GPIO_Init(DRV8301_DCCAL_PORT, DRV8301_DCCAL_PIN, &stcGpioInit);

    /* INL ��������: DRV8301 3-PWM ģʽ��Ϊ�޹ؽš� */
    (void)GPIO_Init(PWM_INL_PORT, PWM_INL_PIN, &stcGpioInit);

    /* �߲� PWM ����: ��Ϊ GPIO ʱ�ȱ��ֵ�, ֮�󽻸���ʱ���� */
    (void)GPIO_Init(PWM_AH_PORT, PWM_AH_PIN, &stcGpioInit);
    (void)GPIO_Init(PWM_BH_PORT, PWM_BH_PIN, &stcGpioInit);
    (void)GPIO_Init(PWM_CH_PORT, PWM_CH_PIN, &stcGpioInit);

    /* SPI nSCS: ��ͨ GPIO, ���иߵ�ƽ�� */
    stcGpioInit.u16PinState = PIN_SET;
    (void)GPIO_Init(DRV8301_SPIx_CS_PORT, DRV8301_SPIx_CS_PIN, &stcGpioInit);

    /* LED �͵�ƽ����, ��ʼΪ�� */
    (void)GPIO_Init(USER_LED_PORT, USER_LED_PIN, &stcGpioInit);

    /* ---- ����: DRV8301 ״̬�� ---- */
    (void)GPIO_StructInit(&stcGpioInit);
    stcGpioInit.u16PinDir  = PIN_DIR_IN;
    stcGpioInit.u16PullUp  = PIN_PU_ON;
    stcGpioInit.u16PinAttr = PIN_ATTR_DIGITAL;

    (void)GPIO_Init(DRV8301_FAULT_PORT, DRV8301_FAULT_PIN, &stcGpioInit);
    (void)GPIO_Init(DRV8301_NOCTW_PORT, DRV8301_NOCTW_PIN, &stcGpioInit);
    (void)GPIO_Init(DRV8301_PWRGD_PORT, DRV8301_PWRGD_PIN, &stcGpioInit);

    /* ---- ����: �������� + ���� EIRQ (PFSR.INTE) ----
       EIRQx �����ź������ж˿ڹ��� (PD2/PB3/PB4 �ֱ��� EIRQ2/3/4),
       ֻ������������ PIN_EXTINT_ON���ر�ע��: EIRQ3 ͬ�����Ż��� PA3
       (���� C), PA3 ����Ĭ�� (���� EXTINT), �����ؾͲ��ᴮ�������жϡ� */
    stcGpioInit.u16ExtInt = PIN_EXTINT_ON;
    (void)GPIO_Init(KEY_1_PORT, KEY_1_PIN, &stcGpioInit);
    (void)GPIO_Init(KEY_2_PORT, KEY_2_PIN, &stcGpioInit);
    (void)GPIO_Init(KEY_3_PORT, KEY_3_PIN, &stcGpioInit);

    /* ������: �Ȱ������������� (����������Ҫ����, �������� 10k);
       Tmra2CaptInit() �ٰ������е� TMRA_2 ���ù���, �������ñ�����
       ע�����ﲻ�� u16ExtInt (���� PA3 ���ػ��� EIRQ3 ���� KEY2)�� */
    stcGpioInit.u16ExtInt = 0U;
    (void)GPIO_Init(HALL_A_PORT, HALL_A_PIN, &stcGpioInit);
    (void)GPIO_Init(HALL_B_PORT, HALL_B_PIN, &stcGpioInit);
    (void)GPIO_Init(HALL_C_PORT, HALL_C_PIN, &stcGpioInit);

    /* ---- SPI2 ���ù��� (����ӹ� SCK/MISO/MOSI, ������ SPI2 ����) ---- */
    (void)GPIO_StructInit(&stcGpioInit);
    stcGpioInit.u16PinDrv  = PIN_HIGH_DRV;
    stcGpioInit.u16PinAttr = PIN_ATTR_DIGITAL;
    (void)GPIO_Init(DRV8301_SPIx_SCK_PORT,  DRV8301_SPIx_SCK_PIN,  &stcGpioInit);
    (void)GPIO_Init(DRV8301_SPIx_MISO_PORT, DRV8301_SPIx_MISO_PIN, &stcGpioInit);
    (void)GPIO_Init(DRV8301_SPIx_MOSI_PORT, DRV8301_SPIx_MOSI_PIN, &stcGpioInit);

    GPIO_SetFunc(DRV8301_SPIx_SCK_PORT,  DRV8301_SPIx_SCK_PIN,  DRV8301_SPIx_SCK_FUNC);
    GPIO_SetFunc(DRV8301_SPIx_MISO_PORT, DRV8301_SPIx_MISO_PIN, DRV8301_SPIx_MISO_FUNC);
    GPIO_SetFunc(DRV8301_SPIx_MOSI_PORT, DRV8301_SPIx_MOSI_PIN, DRV8301_SPIx_MOSI_FUNC);
}

/* ========================== DRV8301 SPI ================================== */

/**
 * @brief  SPI2 ����Ϊ 16 λ����, ģʽ 1 (CPOL=0, CPHA=1), MSB ��ǰ��
 * @param  [in] u32Baud  DRV8301_SPI_BR_x (PCLK1 = 100MHz)
 */
static void Spi2Init(uint32_t u32Baud)
{
    stc_spi_init_t stcSpiInit;

    FCG_Fcg1PeriphClockCmd(FCG1_PERIPH_SPI2, ENABLE);
    SPI_Cmd(DRV8301_SPI_UNIT, DISABLE);

    (void)SPI_StructInit(&stcSpiInit);
    stcSpiInit.u32WireMode          = SPI_4_WIRE;    /* NSS δ����; �ر�ģʽ���ϼ�� */
    stcSpiInit.u32TransMode         = SPI_FULL_DUPLEX;
    stcSpiInit.u32MasterSlave       = SPI_MASTER;
    stcSpiInit.u32ModeFaultDetect   = SPI_MD_FAULT_DETECT_DISABLE;
    stcSpiInit.u32Parity            = SPI_PARITY_INVD;
    stcSpiInit.u32SpiMode           = SPI_MD_1;
    stcSpiInit.u32BaudRatePrescaler = u32Baud;
    stcSpiInit.u32DataBits          = SPI_DATA_SIZE_16BIT;
    stcSpiInit.u32FirstBit          = SPI_FIRST_MSB;
    stcSpiInit.u32FrameLevel        = SPI_1_FRAME;
    (void)SPI_Init(DRV8301_SPI_UNIT, &stcSpiInit);
    SPI_Cmd(DRV8301_SPI_UNIT, ENABLE);
}

/**
 * @brief  ȫ˫�� 16 λ���� (һ֡ DRV8301 ����)��
 * @retval �յ���������; ���賬ʱδ��ɷ��� 0��
 */
static uint16_t Spi2Transfer16(uint16_t u16Tx)
{
    uint32_t u32Timeout;

    u32Timeout = DRV8301_SPI_TIMEOUT;
    while (SPI_GetStatus(DRV8301_SPI_UNIT, SPI_FLAG_TX_BUF_EMPTY) == RESET) {
        if (--u32Timeout == 0UL) {
            return 0U;
        }
    }

    SPI_WriteData(DRV8301_SPI_UNIT, (uint32_t)u16Tx);

    u32Timeout = DRV8301_SPI_TIMEOUT;
    while (SPI_GetStatus(DRV8301_SPI_UNIT, SPI_FLAG_RX_BUF_FULL) == RESET) {
        if (--u32Timeout == 0UL) {
            return 0U;
        }
    }

    return (uint16_t)SPI_ReadData(DRV8301_SPI_UNIT);
}

/**
 * @brief  ��֡ 16 λ����֮�� nSCS ���ָߵ�ƽ��ʱ�� (���� DRV8301 �� tCSH)��
 */
static void SpiDelay(void)
{
    DDL_DelayUS(1UL);
}

/**
 * @brief  DRV8301 �Ĵ���д (addr[12:11] | data[10:0])��
 * @retval SPI ���䳬ʱ���� 0, ���򷵻� 1��
 */
uint8_t Drv8301Write(uint8_t u8Addr, uint16_t u16Data)
{
    uint16_t u16Frame;

    u16Frame = (uint16_t)(((uint16_t)(u8Addr & 0x03U) << 11U) | (u16Data & 0x07FFU));

    DRV_CS_LOW();
    SpiDelay();                 /* ��һ�� SCLK ��֮ǰ nSCS �Ľ���ʱ�� */
    (void)Spi2Transfer16(u16Frame);
    DRV_CS_HIGH();
    SpiDelay();

    return 1U;
}

/**
 * @brief  DRV8301 �Ĵ�����: ��һ֡��������, �ڶ�֡�Ƴ���Ӧ���ݡ�
 */
uint16_t Drv8301Read(uint8_t u8Addr)
{
    uint16_t u16Frame;
    uint16_t u16Data;

    u16Frame = (uint16_t)(0x8000U | ((uint16_t)(u8Addr & 0x03U) << 11U));

    DRV_CS_LOW();
    SpiDelay();
    (void)Spi2Transfer16(u16Frame);
    DRV_CS_HIGH();
    SpiDelay();

    /* �ڶ�֡: ��Ӧ���� nSCS ����ʱװ����λ�Ĵ��� */
    DRV_CS_LOW();
    SpiDelay();
    u16Data = Spi2Transfer16(0x0000U);
    DRV_CS_HIGH();
    SpiDelay();

    return u16Data;
}

/* ============================== ADC ====================================== */

/**
 * @brief  ADC1 ���� A: I_total, VBUS, IA, IB, NTC �� 5 ͨ����
 *         Դ����: ADC1 ע���� (VBUS/IA/IB/ITOTAL, TIM1_CC4 ����) + DMA ������
 *         (U/V/W ���ѹ + NTC)������û�� U/V/W ���ѹ������·, �� ADC1 ��
 *         DMA, ���� 5 ͨ���ϳ�һ������ A, �� PWM �ж�����������
 */
static void AdcInit(void)
{
    stc_adc_init_t stcAdcInit;
    stc_gpio_init_t stcGpioInit;

    FCG_Fcg3PeriphClockCmd(FCG3_PERIPH_ADC1, ENABLE);

    /* ģ������: PC0..PC3 (VBUS/IA/IB/NTC) + PA2 (I_total) */
    (void)GPIO_StructInit(&stcGpioInit);
    stcGpioInit.u16PinAttr = PIN_ATTR_ANALOG;
    (void)GPIO_Init(VBUS_ADC_PORT, (VBUS_ADC_PIN | IA_CURRENT_ADC_PIN | IB_CURRENT_ADC_PIN | NTC_ADC_PIN), &stcGpioInit);
    (void)GPIO_Init(ITOTAL_ADC_PORT, ITOTAL_ADC_PIN, &stcGpioInit);

    CLK_SetPeriClockSrc(CLK_PERIPHCLK_PCLK);

    (void)ADC_StructInit(&stcAdcInit);
    (void)ADC_Init(CM_ADC1, &stcAdcInit);

    ADC_ChCmd(CM_ADC1, ADC_SEQ_A, ADC_ITOTAL_CH, ENABLE);
    ADC_ChCmd(CM_ADC1, ADC_SEQ_A, ADC_VBUS_CH,   ENABLE);
    ADC_ChCmd(CM_ADC1, ADC_SEQ_A, ADC_IA_CH,     ENABLE);
    ADC_ChCmd(CM_ADC1, ADC_SEQ_A, ADC_IB_CH,     ENABLE);
    ADC_ChCmd(CM_ADC1, ADC_SEQ_A, ADC_NTC_CH,    ENABLE);

    ADC_SetSampleTime(CM_ADC1, ADC_ITOTAL_CH, ADC_SAMPLE_TIME);
    ADC_SetSampleTime(CM_ADC1, ADC_VBUS_CH,   ADC_SAMPLE_TIME);
    ADC_SetSampleTime(CM_ADC1, ADC_IA_CH,     ADC_SAMPLE_TIME);
    ADC_SetSampleTime(CM_ADC1, ADC_IB_CH,     ADC_SAMPLE_TIME);
    ADC_SetSampleTime(CM_ADC1, ADC_NTC_CH,    ADC_SAMPLE_TIME);
}

/* ============================== PWM ====================================== */

/**
 * @brief  20kHz TMRA_1 ����ж�, ���������ǲ����㡣
 *
 * Դ����: ADC ע������ TIM1_CC4 Ӳ������ + JEOC �жϡ�
 * ����ֲ: ��ͬһʱ�� (�¹ܵ�ͨ����, �����������Ч) �ɱ��ж���������
 * ���� A; ͬʱ�ڴ˲��� 1ms ʱ�� (Դ������ SysTick ����)��
 */
static void Tmra1OvfIrqCallback(void)
{
    TMRA_ClearStatus(CM_TMRA_1, TMRA_FLAG_OVF);

    if (g_u8AdcBusy == 0U) {
        g_u8AdcBusy = 1U;
        (void)ADC_Start(CM_ADC1);
    }

    /* PC host link sampling tick (20 kHz, internally divided per host rate). */
    communication_task();

    if (++s_u32PwmCnt >= (PWM_TIM_FREQ / 1000UL)) {
        s_u32PwmCnt = 0UL;
        g_u8Tick1ms = 1U;           /* ��ѭ�������� */
    }
}

/**
 * @brief  ע��һ�������ж� (INTC ǩ�� + NVIC ʹ��)��
 */
static void IntcIrqInstall(const stc_irq_signin_config_t *pstcConfig, uint32_t u32Priority)
{
    if (NULL != pstcConfig) {
        (void)INTC_IrqSignIn(pstcConfig);
        NVIC_ClearPendingIRQ(pstcConfig->enIRQn);
        NVIC_SetPriority(pstcConfig->enIRQn, u32Priority);
        NVIC_EnableIRQ(pstcConfig->enIRQn);
    }
}

/**
 * @brief  TMRA_1 ���Ķ��� PWM, 20kHz, ��ͨ����ʼ 50% (���ģ��ѹ)��
 *         Դ����: TIM1 ���Ķ��� 10kHz ���� PWM + BDTR ����; ���� DRV8301
 *         Ϊ 3-PWM ģʽ (INL ����, �������������ڲ�����)��
 *
 * EN_GATE Ϊ���ڼ䶨ʱ���ճ����� (�����ż��ض�), ��Դ���� "������ PWM
 * ��ʹ�� DRV8301" ��˳��һ�¡�
 */
static void PwmInit(void)
{
    stc_tmra_init_t stcTmraInit;
    stc_tmra_pwm_init_t stcPwmInit;
    stc_irq_signin_config_t stcIrqSigninConfig;

    FCG_Fcg2PeriphClockCmd(FCG2_PERIPH_TMRA_1, ENABLE);

    /* PA8/PA9/PA10 -> TIMA_1_PWM1/2/3, ���� 4 */
    GPIO_SetFunc(PWM_AH_PORT, PWM_AH_PIN, PWM_GPIO_FUNC);
    GPIO_SetFunc(PWM_BH_PORT, PWM_BH_PIN, PWM_GPIO_FUNC);
    GPIO_SetFunc(PWM_CH_PORT, PWM_CH_PIN, PWM_GPIO_FUNC);

    (void)TMRA_StructInit(&stcTmraInit);
    stcTmraInit.sw_count.u8ClockDiv  = TMRA_CLK_DIV1;
    stcTmraInit.sw_count.u8CountMode = TMRA_MD_TRIANGLE;    /* ���Ķ��� (��ӦԴ TIM_CounterMode_CenterAligned1) */
    stcTmraInit.sw_count.u8CountDir  = TMRA_DIR_UP;
    stcTmraInit.u32PeriodValue       = PWM_TIM_PULSE;
    (void)TMRA_Init(CM_TMRA_1, &stcTmraInit);

    (void)TMRA_PWM_StructInit(&stcPwmInit);
    stcPwmInit.u32CompareValue         = PWM_TIM_PULSE / 2UL;   /* 50% ռ�ձ� */
    stcPwmInit.u16StartPolarity        = TMRA_PWM_HIGH;
    stcPwmInit.u16StopPolarity         = TMRA_PWM_LOW;
    stcPwmInit.u16CompareMatchPolarity = TMRA_PWM_INVT;
    stcPwmInit.u16PeriodMatchPolarity  = TMRA_PWM_HOLD;

    (void)TMRA_PWM_Init(CM_TMRA_1, TMRA_CH1, &stcPwmInit);
    (void)TMRA_PWM_Init(CM_TMRA_1, TMRA_CH2, &stcPwmInit);
    (void)TMRA_PWM_Init(CM_TMRA_1, TMRA_CH3, &stcPwmInit);

    /* 20kHz �����ж� = ADC ���� + 1ms ʱ����
       (Դ����: NVIC ADC_IRQn ��ռ���ȼ� 1, Ϊ��������ȼ��жϡ�) */
    stcIrqSigninConfig.enIRQn      = INT080_IRQn;
    stcIrqSigninConfig.enIntSrc    = INT_SRC_TMRA_1_OVF;
    stcIrqSigninConfig.pfnCallback = &Tmra1OvfIrqCallback;
    IntcIrqInstall(&stcIrqSigninConfig, DDL_IRQ_PRIO_03);

    TMRA_IntCmd(CM_TMRA_1, TMRA_INT_OVF, ENABLE);

    TMRA_SetCountValue(CM_TMRA_1, 1UL);
    TMRA_Start(CM_TMRA_1);
}

/* ========================= �������� (TMRA_2) ============================= */

/**
 * @brief  TMRA_2 ����ж�: 16 λ����������ʱ��������λ��һ��
 *
 * ��ӦԴ���� TIM2 �� 32 λ������: HC32 TMRA �� 16 λ (PCLK1/16 = 6.25MHz,
 * �������� 10.49ms), ������λ������չ�� 32 λ, ����������Դ������ͬ��
 */
static void Tmra2OvfIrqCallback(void)
{
    TMRA_ClearStatus(HALL_CAPT_UNIT, TMRA_FLAG_OVF);
    s_u32HallOvfCnt++;
}

/**
 * @brief  TMRA_2 �����ж� (INT_SRC_TMRA_2_CMP, ����ͨ������һ���¼�Դ)��
 *
 * ÿ�������ؽ�һ��: ȡ����Ĳ���ֵ -> ������չ�� 32 λʱ��� ->
 * ����һ������õ���� (��ӦԴ����"��ģʽ��λ + CCR1"������) ->
 * �������ߵ�ƽ�������� (Դ���� TIM2_IRQHandler ͬ���� GPIO ������)��
 * �����Ǹ�����, ������ÿ��ֻ��һ����; ����������ͬʱ��ת, ���жϰ�
 * ����ͨ���ı�־��������, ʱ���ȡ��һ����λͨ��������ֵ��
 */
static void Tmra2CaptIrqCallback(void)
{
    uint32_t u32Ch = s_au32HallCh[0];
    uint16_t u16Cap;
    uint32_t u32Now;
    uint8_t  u8EdgeSeen = 0U;
    uint8_t  i;

    for (i = 0U; i < 3U; i++) {
        if (TMRA_GetStatus(HALL_CAPT_UNIT, TMRA_FLAG_CMP_CH1 << s_au32HallCh[i]) == SET) {
            TMRA_ClearStatus(HALL_CAPT_UNIT, TMRA_FLAG_CMP_CH1 << s_au32HallCh[i]);
            if (u8EdgeSeen == 0U) {
                u32Ch = s_au32HallCh[i];    /* ȡ��һ����λͨ��������ֵ */
                u8EdgeSeen = 1U;
            }
        }
    }
    if (u8EdgeSeen == 0U) {
        return;                             /* �޲����־, ��Ӧ���� */
    }

    u16Cap = (uint16_t)TMRA_GetCompareValue(HALL_CAPT_UNIT, u32Ch);
    u32Now = (s_u32HallOvfCnt << 16) | (uint32_t)u16Cap;
    /* ��̬����: ����ֵ��С�� OVF ��־������ -> ����շ����� OVF �ж�
       ��δִ��, ������λ�ټ���һ��, ���ﲹ�� 0x10000�� */
    if ((TMRA_GetStatus(HALL_CAPT_UNIT, TMRA_FLAG_OVF) == SET) && (u16Cap < 0x8000U)) {
        u32Now += 0x10000UL;
    }

    if (s_u8HallEdgeValid != 0U) {
        g_u32HallEdgeInterval = u32Now - s_u32HallLastEdge;
    } else {
        g_u32HallEdgeInterval = 0U;         /* ��һ��ֻ��ʱ��� */
        s_u8HallEdgeValid = 1U;
    }
    s_u32HallLastEdge = u32Now;
    g_u32HallEdgeCnt++;

    /* ����: ���϶������ߵ�ƽ��HC32 �������ݼĴ����빦��ѡ�����,
       �����е� Func5 �������ܶ���ƽ (�״ε��ʱ˳����֤)�� */
    g_u8HallSector = Board_HallRead();
}

/**
 * @brief  TMRA_2 ��ͨ����������, ��ӦԴ���� TIM2 �����ӿڡ�
 *
 *   Դ���� (STM32 TIM2)              ���� (TMRA_2)
 *   -----------------------          -----------------------------
 *   3 ͨ����� (TI1S) ��һ·          ����Ҫ: ÿͨ�����������Լ���
 *                                    PWM ����, 6 ��/������������
 *   TI1 ˫����                       HICP0|HICP1 Ӳ��˫�� (λ��
 *                                    ����ʹ��, ������)
 *   ��ģʽ��λ, CCR1 = ���ؼ��       ������������, ���������ڲ�
 *   32 λ���� @90MHz                  16 λ @6.25MHz + ���������չ
 *
 * ����: PA0/PA1/PA3 -> Func5 (TIMA_2_PWM1/2/4)���������� GpioInit()
 * ���, GPIO_SetFunc ֻ�� FSEL, �������� (����������Ҫ)��
 */
static void Tmra2CaptInit(void)
{
    stc_tmra_init_t stcTmraInit;
    stc_irq_signin_config_t stcIrqSigninConfig;
    uint8_t i;

    HALL_CAPT_FCG_ENABLE();

    /* �����������е� TMRA_2 ���ù��� (��������) */
    GPIO_SetFunc(HALL_A_PORT, HALL_A_PIN, HALL_CAPT_GPIO_FUNC);
    GPIO_SetFunc(HALL_B_PORT, HALL_B_PIN, HALL_CAPT_GPIO_FUNC);
    GPIO_SetFunc(HALL_C_PORT, HALL_C_PIN, HALL_CAPT_GPIO_FUNC);

    /* �����ܼ�����: ��ݲ�����, ���� 0xFFFF ���� (6.25MHz -> 10.49ms) */
    (void)TMRA_StructInit(&stcTmraInit);
    stcTmraInit.sw_count.u8ClockDiv  = HALL_CAPT_CLK_DIV;
    stcTmraInit.sw_count.u8CountMode = TMRA_MD_SAWTOOTH;
    stcTmraInit.sw_count.u8CountDir  = TMRA_DIR_UP;
    stcTmraInit.u32PeriodValue       = 0xFFFFUL;
    (void)TMRA_Init(HALL_CAPT_UNIT, &stcTmraInit);

    /* ����ͨ��: ����ģʽ + Ӳ��˫�� (������|�½���ͬʱʹ��) */
    for (i = 0U; i < 3U; i++) {
        TMRA_SetFunc(HALL_CAPT_UNIT, s_au32HallCh[i], TMRA_FUNC_CAPT);
        TMRA_HWCaptureCondCmd(HALL_CAPT_UNIT, s_au32HallCh[i], HALL_CAPT_COND_BOTH, ENABLE);
    }

    /* ����ж� (��λ) + �����жϡ������� DDL_IRQ_PRIO_03, �� TMRA_1 OVF
       ͬ��: ͬ����������ռ, �����ز����Ͻ����� FOC �������� */
    stcIrqSigninConfig.enIRQn      = HALL_CAPT_OVF_IRQn;
    stcIrqSigninConfig.enIntSrc    = HALL_CAPT_OVF_INT_SRC;
    stcIrqSigninConfig.pfnCallback = &Tmra2OvfIrqCallback;
    IntcIrqInstall(&stcIrqSigninConfig, DDL_IRQ_PRIO_04);

    stcIrqSigninConfig.enIRQn      = HALL_CAPT_CMP_IRQn;
    stcIrqSigninConfig.enIntSrc    = HALL_CAPT_CMP_INT_SRC;
    stcIrqSigninConfig.pfnCallback = &Tmra2CaptIrqCallback;
    IntcIrqInstall(&stcIrqSigninConfig, DDL_IRQ_PRIO_03);

    TMRA_IntCmd(HALL_CAPT_UNIT, (TMRA_INT_OVF | TMRA_INT_CMP_CH1 | TMRA_INT_CMP_CH2 | TMRA_INT_CMP_CH4), ENABLE);

    TMRA_SetCountValue(HALL_CAPT_UNIT, 0UL);
    TMRA_Start(HALL_CAPT_UNIT);
}

/* ========================= ���� EXTI (EIRQ) ============================== */

/**
 * @brief  ���� 1 �ж� (KEY1 = PD2 = EIRQ2, �½���)��
 *
 * ����ͬԴ����: �� key1_flag��������α����ж�, �� Board_Key1msTask()
 * ��ʱ������������װ (��е�����ɲ�������½���, ���δ�����ȫ������)��
 */
static void Key1IrqCallback(void)
{
    EXTINT_ClearExtIntStatus(KEY_1_EXTINT_CH);
    g_u8Key1Flag = 1U;
    s_u16Key1DbCnt = 1U;
    NVIC_DisableIRQ(KEY_1_IRQn);
}

static void Key2IrqCallback(void)
{
    EXTINT_ClearExtIntStatus(KEY_2_EXTINT_CH);
    g_u8Key2Flag = 1U;
    s_u16Key2DbCnt = 1U;
    NVIC_DisableIRQ(KEY_2_IRQn);
}

static void Key3IrqCallback(void)
{
    EXTINT_ClearExtIntStatus(KEY_3_EXTINT_CH);
    g_u8Key3Flag = 1U;
    s_u16Key3DbCnt = 1U;
    NVIC_DisableIRQ(KEY_3_IRQn);
}

/**
 * @brief  ���� EXTI ��ʼ�� (Դ����: SYSCFG_EXTILineConfig + EXTI_Init
 *         �½��� + NVIC ʹ��; ����: GPIO PFSR.INTE + EXTINT_Init +
 *         INTC ǩ��)��Ӳ���˲��� (����ʱ�� /8) ��խ����, ms ������
 *         �����������ڡ����ȼ� DDL_IRQ_PRIO_04: ��������, ���� PWM/������
 */
static void KeyExtiInit(void)
{
    stc_extint_init_t stcExtIntInit;
    stc_irq_signin_config_t stcIrqSigninConfig;

    (void)EXTINT_StructInit(&stcExtIntInit);
    stcExtIntInit.u32Filter      = EXTINT_FILTER_ON;
    stcExtIntInit.u32FilterClock = EXTINT_FCLK_DIV8;
    stcExtIntInit.u32Edge        = EXTINT_TRIG_FALLING;

    (void)EXTINT_Init(KEY_1_EXTINT_CH, &stcExtIntInit);
    (void)EXTINT_Init(KEY_2_EXTINT_CH, &stcExtIntInit);
    (void)EXTINT_Init(KEY_3_EXTINT_CH, &stcExtIntInit);

    stcIrqSigninConfig.enIRQn      = KEY_1_IRQn;
    stcIrqSigninConfig.enIntSrc    = KEY_1_INT_SRC;
    stcIrqSigninConfig.pfnCallback = &Key1IrqCallback;
    IntcIrqInstall(&stcIrqSigninConfig, DDL_IRQ_PRIO_04);

    stcIrqSigninConfig.enIRQn      = KEY_2_IRQn;
    stcIrqSigninConfig.enIntSrc    = KEY_2_INT_SRC;
    stcIrqSigninConfig.pfnCallback = &Key2IrqCallback;
    IntcIrqInstall(&stcIrqSigninConfig, DDL_IRQ_PRIO_04);

    stcIrqSigninConfig.enIRQn      = KEY_3_IRQn;
    stcIrqSigninConfig.enIntSrc    = KEY_3_INT_SRC;
    stcIrqSigninConfig.pfnCallback = &Key3IrqCallback;
    IntcIrqInstall(&stcIrqSigninConfig, DDL_IRQ_PRIO_04);
}

/**
 * @brief  ��������/����װ����, ��ѭ�� 1ms ��֧����á�
 *
 * �������� KEY_DEBOUNCE_MS ���ں�: ��������ڼ��ѹ�ı���״̬��־,
 * ����ʹ�ܱ����жϡ�����ʱ���Ա���ס (����), ���ű��ֵ�, ���������
 * �½���, ����һ�ΰ���ֻ��һ�α�־ (��Դ������Ϊһ��)��
 */
void Board_Key1msTask(void)
{
    if (s_u16Key1DbCnt != 0U) {
        if (++s_u16Key1DbCnt >= KEY_DEBOUNCE_MS) {
            s_u16Key1DbCnt = 0U;
            EXTINT_ClearExtIntStatus(KEY_1_EXTINT_CH);
            NVIC_ClearPendingIRQ(KEY_1_IRQn);
            NVIC_EnableIRQ(KEY_1_IRQn);
        }
    }
    if (s_u16Key2DbCnt != 0U) {
        if (++s_u16Key2DbCnt >= KEY_DEBOUNCE_MS) {
            s_u16Key2DbCnt = 0U;
            EXTINT_ClearExtIntStatus(KEY_2_EXTINT_CH);
            NVIC_ClearPendingIRQ(KEY_2_IRQn);
            NVIC_EnableIRQ(KEY_2_IRQn);
        }
    }
    if (s_u16Key3DbCnt != 0U) {
        if (++s_u16Key3DbCnt >= KEY_DEBOUNCE_MS) {
            s_u16Key3DbCnt = 0U;
            EXTINT_ClearExtIntStatus(KEY_3_EXTINT_CH);
            NVIC_ClearPendingIRQ(KEY_3_IRQn);
            NVIC_EnableIRQ(KEY_3_IRQn);
        }
    }
}

/* ============================ �弶����ӿ� ============================== */

/**
 * @brief  д����ռ�ձ�, ÿ�� 0.0 ~ 1.0��
 *         (Դ����: FOC ����ֱ��д PWM_TIM->CCR1/2/3��)
 */
void Board_PwmSetDuty(float fA, float fB, float fC)
{
    if (fA < 0.0f) { fA = 0.0f; }
    if (fA > 1.0f) { fA = 1.0f; }
    if (fB < 0.0f) { fB = 0.0f; }
    if (fB > 1.0f) { fB = 1.0f; }
    if (fC < 0.0f) { fC = 0.0f; }
    if (fC > 1.0f) { fC = 1.0f; }

    TMRA_SetCompareValue(CM_TMRA_1, TMRA_CH1, (uint32_t)(fA * (float)PWM_TIM_PULSE));
    TMRA_SetCompareValue(CM_TMRA_1, TMRA_CH2, (uint32_t)(fB * (float)PWM_TIM_PULSE));
    TMRA_SetCompareValue(CM_TMRA_1, TMRA_CH3, (uint32_t)(fC * (float)PWM_TIM_PULSE));
}

/**
 * @brief  PWM �������/�ء�STM32: TIM_CtrlPWMOutputs(PWM_TIM, x)��
 *         �κ�ʱ�̵��ö���ȫ: EN_GATE Ϊ��ʱ�ż�ʼ�չضϡ�
 */
void Board_MotorPwmEnable(void)
{
    TMRA_PWM_OutputCmd(CM_TMRA_1, TMRA_CH1, ENABLE);
    TMRA_PWM_OutputCmd(CM_TMRA_1, TMRA_CH2, ENABLE);
    TMRA_PWM_OutputCmd(CM_TMRA_1, TMRA_CH3, ENABLE);
}

void Board_MotorPwmDisable(void)
{
    TMRA_PWM_OutputCmd(CM_TMRA_1, TMRA_CH1, DISABLE);
    TMRA_PWM_OutputCmd(CM_TMRA_1, TMRA_CH2, DISABLE);
    TMRA_PWM_OutputCmd(CM_TMRA_1, TMRA_CH3, DISABLE);
    Board_PwmSetDuty(0.5f, 0.5f, 0.5f);
}

/**
 * @brief  ������״̬, bit0 = A, bit1 = B, bit2 = C��
 *         ������ Func5 (TMRA_2 ����) �������ɶ� - ����Ĵ����빦��ѡ�������
 */
uint8_t Board_HallRead(void)
{
    uint8_t u8Hall = 0U;

    if (GPIO_ReadInputPins(HALL_A_PORT, HALL_A_PIN) == PIN_SET) { u8Hall |= 0x01U; }
    if (GPIO_ReadInputPins(HALL_B_PORT, HALL_B_PIN) == PIN_SET) { u8Hall |= 0x02U; }
    if (GPIO_ReadInputPins(HALL_C_PORT, HALL_C_PIN) == PIN_SET) { u8Hall |= 0x04U; }

    return u8Hall;
}

/**
 * @brief  �弶Ӳ����ʼ��, Դ���� hard_init() ����ֲ��
 *
 * ��������: ����Ĵ����Դ���д����״̬ (main ���ȵ� LL_PERIPH_WE(...),
 * ������� LL_PERIPH_FCG �� LL_PERIPH_GPIO)��SystemClockInit() ִ����
 * ֮�� DDL_DelayMS/US ��ʱ����ʹ�á�
 * USB �豸ջ (usb_dev_init, �� usb_bsp ����/ʱ��/����̨ + CDC ��) ��
 * main �� hard_init() ֮�󡢼Ĵ���д����֮ǰ���á�
 */
void hard_init(void)
{
    SystemClockInit();                  /* Դ: RCC_GetClocksFreq + ��ʱ��ʹ�� (�� USB PLLX) */
    GpioInit();                         /* Դ: DRV8301 / OLED / LED / ���� / �������� */
    Spi2Init(DRV8301_SPI_BR_FAST);      /* Դ: SPI ������ drv8301_init() ��, ������ǰ���弶 */
    AdcInit();                          /* Դ: ADC + DMA ���� */
    PwmInit();                          /* Դ: TIM1 PWM + NVIC ���ȼ����� */
    Tmra2CaptInit();                    /* Դ: TIM2 �����ӿ� */
    KeyExtiInit();                      /* Դ: ���� EXTI + NVIC ���� */

    /*
     * Դ�����ڴ˴����� EN_GATE (GPIO_SetBits)��
     * ���屣�������ż��ض�, ֱ�� drv8301_init() ͨ�� SPI ���ò�У��
     * DRV8301 �Ĵ���֮����ʹ�� (�� drv8301_test �Ĳ���һ��)��
     */
    EN_GATE_LOW();
    DC_CAL_LOW();
    USER_LED_OFF();
}

/*******************************************************************************
 * EOF (not truncated)
 ******************************************************************************/
