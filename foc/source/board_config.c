/**
 *******************************************************************************
 * @file  foc/source/board_config.c
 * @brief hard_init() - HC32F460KETA + DRV8301 板级初始化与时钟配置
 *
 * 移植自 STM32F4 原工程:
 *   配套代码/代码/code/user/board_config.c  (hard_init)
 *
 * 本工程与原工程初始化项的对应关系 (hard_init 中完成):
 *   原工程 (STM32F446)                    本工程 (HC32F460)
 *   -------------------------             ---------------------------
 *   RCC 使能 GPIO/TIM/ADC/DMA 时钟         FCG 使能各外设时钟
 *   SysTick_Config(HCLK/1000)             TMRA_1 溢出中断, 派生 1ms 节拍
 *   TIM2 霍尔接口 (TI1 双沿 + 滤波)        TMRA_2 捕获 (CH1/CH2/CH4 对应
 *                                         PA0/PA1/PA3) 双沿捕获 + 溢出计数
 *                                         + 边沿间隔; 扇区直接读 GPIO 输入
 *                                         (见原 hall_sensor.c TIM2_IRQHandler)
 *   DRV8301 使能 GPIO 与复位              SPI2 + GPIO, EN_GATE 先拉低
 *   OLED SPI 驱动                         OLED 走 I2C PC7/PC8 (待移植)
 *   USER LED PC9                          PC9, 心跳指示
 *   TIM1 6 路 PWM + BDTR 死区             TMRA_1 3 路 PWM (DRV8301 3-PWM
 *                                         模式), 20kHz
 *   ADC1 注入组 (T1_CC4 触发) + DMA       ADC1 序列 A, TMRA_1 OVF 事件经 AOS
 *                                         硬件触发 + DMA1 CH0 搬 DR2 起 12 个
 *   ---- EXTI 按键 + NVIC 中断            按键 EXTI (EIRQ2/3/4),
 *                                         采用 消抖 + 1ms 轮询 方式处理
 *   USB PC 链路 (pc_communication_init)   usb_dev_init() 在 main 里调用
 *                                         (见 usb_bsp.c/cdc_data_process.c)
 *   ADC JEOC 中断                         DMA 块完成中断 (INT038) 跑 communication_task
 *
 * 注意: 本工程 hard_init() 不拉高 EN_GATE (默认不使能功率级),
 * 只完成引脚初始化; 真正使能 (如 drv8301_test 那样) 需在:
 * 完成 DRV8301 的 SPI 配置之后 (即调用 drv8301_init() 之后)
 * 再拉高 EN_GATE, 否则功率级不受控。
 *******************************************************************************
 */

#include "board_config.h"
#include "pc_communication.h"   /* communication_task(): PC host link sampling */
#include "pc_communication_init.h"   /* pc_test_sine_task() */

/*******************************************************************************
 * 全局变量
 ******************************************************************************/
/* ADC 序列 A 的 DMA 结果 (从 DR2 搬 12 个 16-bit):
   [0]=CH2 ITOTAL, [8]=CH10 VBUS, [9]=CH11 IA, [10]=CH12 IB, [11]=CH13 NTC */
volatile uint16_t g_au16AdcValue[ADC_DMA_BLOCK_SIZE] = {0U};
volatile uint8_t g_u8Tick1ms = 0U;

/* TMRA_2 捕获结果 (用法见原 hall_sensor.c) */
volatile uint32_t g_u32HallEdgeInterval = 0U;   /* 0 = 尚无有效边沿 */
volatile uint32_t g_u32HallEdgeCnt = 0U;
volatile uint8_t  g_u8HallSector = 0U;

/* 按键标志 (对应原工程的 key1_flag/key2_flag/key3_flag) */
volatile uint8_t g_u8Key1Flag = 0U;
volatile uint8_t g_u8Key2Flag = 0U;
volatile uint8_t g_u8Key3Flag = 0U;

static uint32_t s_u32PwmCnt = 0UL;      /* 20kHz 计数, 用于派生 1ms 节拍 */

/* 霍尔捕获溢出计数的扩展 */
static uint32_t s_u32HallOvfCnt = 0UL;  /* 溢出扩展: 16 位计数器的高位 */
static uint32_t s_u32HallLastEdge = 0UL;/* 上一次捕获的完整计数值 */
static uint8_t  s_u8HallEdgeValid = 0U; /* 0: 尚未收到首个边沿, 忽略间隔 */

/* 按键消抖计数: 非 0 表示正在消抖 (由对应 EXTI 回调置 1), 由 1ms 任务递增 */
static volatile uint16_t s_u16Key1DbCnt = 0U;
static volatile uint16_t s_u16Key2DbCnt = 0U;
static volatile uint16_t s_u16Key3DbCnt = 0U;

/* 霍尔捕获通道 (CH3 对应 PA2 第二功能, 见 TP181 规格书) */
static const uint32_t s_au32HallCh[3] = { HALL_CAPT_CH_A, HALL_CAPT_CH_B, HALL_CAPT_CH_C };

/*******************************************************************************
 * 本地函数声明 ('static')
 ******************************************************************************/
static void SystemClockInit(void);
static void GpioInit(void);
static void Spi2Init(uint32_t u32Baud);
static uint16_t Spi2Transfer16(uint16_t u16Tx);
static void AdcInit(void);
static void AdcHardTriggerConfig(void);
static void AdcDmaConfig(void);
static void AdcDmaIrqCallback(void);
static void IntcIrqInstall(const stc_irq_signin_config_t *pstcConfig, uint32_t u32Priority);
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
 * 函数实现
 ******************************************************************************/

/**
 * @brief  时钟: 外部 16MHz -> MPLL 200MHz + PLLX 48MHz (USB 用),
 *         PCLK1 100MHz。
 *         (原工程: RCC_GetClocksFreq + 180MHz PLL, 8M 晶振;
 *         本工程: 16MHz 晶振, 200MHz 主频;
 *         PLLX 48MHz 沿用 usb_dev_cdc 的配置, USB FS 需精确 48MHz,
 *         与主 PLL 相互独立)
 */
static void SystemClockInit(void)
{
    stc_clock_xtal_init_t stcXtalInit;
    stc_clock_pll_init_t  stcMpllInit;
    stc_clock_pllx_init_t stcUpllInit;
    uint32_t u32Wait;

    /* 时钟分频: HCLK=200MHz, PCLK1=100MHz, PCLK2=50MHz */
    CLK_SetClockDiv(CLK_BUS_CLK_ALL, (CLK_HCLK_DIV1 | CLK_EXCLK_DIV2 | CLK_PCLK0_DIV1 | \
                                      CLK_PCLK1_DIV2 | CLK_PCLK2_DIV4 | CLK_PCLK3_DIV4 | CLK_PCLK4_DIV2));

    /* 使能外部晶振输入, 配置为 16MHz 无源晶振 */
    GPIO_AnalogCmd(XTAL_PORT, XTAL_PIN, ENABLE);
    (void)CLK_XtalStructInit(&stcXtalInit);
    stcXtalInit.u8State      = CLK_XTAL_ON;
    stcXtalInit.u8Mode       = CLK_XTAL_MD_OSC;
    stcXtalInit.u8Drv        = CLK_XTAL_DRV_MID;    /* 16MHz 落在 [16,20) 区间, 用中档驱动能力 */
    stcXtalInit.u8StableTime = CLK_XTAL_STB_2MS;
    (void)CLK_XtalInit(&stcXtalInit);

    /* 等待晶振稳定, 超时则死等 (避免时钟异常导致无法调试) */
    u32Wait = 100UL;
    while ((SET != CLK_GetStableStatus(CLK_STB_FLAG_XTAL)) && (u32Wait > 0UL)) {
        DDL_DelayMS(1UL);
        u32Wait--;
    }
    if (SET != CLK_GetStableStatus(CLK_STB_FLAG_XTAL)) {
        for (;;) {
        }
    }

    /* MPLL 配置: 16MHz / 2 * 50 / 2 = 200MHz (VCO = 400MHz) */
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

    /* PLLX (UPLL): USB FS 专用时钟 16MHz / 4 * 84 / 7 = 48MHz。
       与主 PLL 的 200MHz / PCLK1 100MHz 相互独立 */
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
        for (;;) {                  /* PLLX 未就绪, USB 无法工作 */
        }
    }

    /* 切到 PLL 前: 先按高频设置 SRAM / Flash / GPIO 等待周期 */
    SRAM_SetWaitCycle(SRAM_SRAMH, SRAM_WAIT_CYCLE0, SRAM_WAIT_CYCLE0);
    SRAM_SetWaitCycle((SRAM_SRAM12 | SRAM_SRAM3 | SRAM_SRAMR), SRAM_WAIT_CYCLE1, SRAM_WAIT_CYCLE1);
    (void)EFM_SetWaitCycle(EFM_WAIT_CYCLE5);        /* 126~200MHz: 5 个等待周期 */
    GPIO_SetReadWaitCycle(GPIO_RD_WAIT3);
    (void)PWC_HighSpeedToHighPerformance();
    CLK_SetSysClockSrc(CLK_SYSCLK_SRC_PLL);         /* 切换系统时钟到 MPLL */

    /* 使能 Flash Cache */
    EFM_CacheRamReset(ENABLE);
    EFM_CacheRamReset(DISABLE);
    EFM_CacheCmd(ENABLE);
}

/**
 * @brief  GPIO 初始化, 在 hard_init 里调用 (配置全部 GPIO)。
 *         (DRV8301 / 按键 / 霍尔 / LED / PWM 等引脚)。
 *         USB DP/DM (PA11/PA12) 在 usb_bsp.c 里配置, 此处不处理。
 */
static void GpioInit(void)
{
    stc_gpio_init_t stcGpioInit;

    /*
     * PA13/PA14/PA15/PB3/PB4 默认复用为 JTAG/SWD 功能, 由 PSPCR.SPFE[4:0] = 0x1F
     关闭全部复用; SPFE 优先于 PFSR.FSEL, 所以要在 GPIO_Init() 前先关闭
     (见 RM 9.4.7 / 9.4.12)。
     说明: PA15 = nSCS, PB3/PB4 = KEY2/KEY3 -> 关闭 JTAG 后仍保留 SWD
     (SPFE[0]=JTCK_SWCLK, SPFE[1]=JTMS_SWDIO)。
     本工程 DRV8301 的 SPI 片选由软件控制, nSCS 接在 PA15 (原 STM32 用
     硬件片选, 这里改为普通 IO)
     */
    GPIO_SetDebugPort((uint8_t)(GPIO_PIN_TDI | GPIO_PIN_TDO | GPIO_PIN_TRST), DISABLE);

    /* ---- 数字输出: EN_GATE 低, DC_CAL 低, LED 熄灭, SPI 片选高 ---- */
    (void)GPIO_StructInit(&stcGpioInit);
    stcGpioInit.u16PinDir        = PIN_DIR_OUT;
    stcGpioInit.u16PinOutputType = PIN_OUT_TYPE_CMOS;
    stcGpioInit.u16PinDrv        = PIN_LOW_DRV;
    stcGpioInit.u16PinState      = PIN_RESET;
    stcGpioInit.u16PullUp        = PIN_PU_OFF;
    stcGpioInit.u16PinAttr       = PIN_ATTR_DIGITAL;

    (void)GPIO_Init(DRV8301_ENGATE_PORT, DRV8301_ENGATE_PIN, &stcGpioInit);
    (void)GPIO_Init(DRV8301_DCCAL_PORT, DRV8301_DCCAL_PIN, &stcGpioInit);

    /* INL 选择脚: DRV8301 3-PWM 模式配置引脚 */
    (void)GPIO_Init(PWM_INL_PORT, PWM_INL_PIN, &stcGpioInit);

    /* 使能 PWM 输出脚: 先配 GPIO 为复用, 具体由 PwmInit 完成 */
    (void)GPIO_Init(PWM_AH_PORT, PWM_AH_PIN, &stcGpioInit);
    (void)GPIO_Init(PWM_BH_PORT, PWM_BH_PIN, &stcGpioInit);
    (void)GPIO_Init(PWM_CH_PORT, PWM_CH_PIN, &stcGpioInit);

    /* SPI nSCS: 普通 GPIO, 软件控制片选 */
    stcGpioInit.u16PinState = PIN_SET;
    (void)GPIO_Init(DRV8301_SPIx_CS_PORT, DRV8301_SPIx_CS_PIN, &stcGpioInit);

    /* LED 推挽输出, 初始熄灭 */
    (void)GPIO_Init(USER_LED_PORT, USER_LED_PIN, &stcGpioInit);

    /* ---- 数字输入: DRV8301 状态/故障 ---- */
    (void)GPIO_StructInit(&stcGpioInit);
    stcGpioInit.u16PinDir  = PIN_DIR_IN;
    stcGpioInit.u16PullUp  = PIN_PU_ON;
    stcGpioInit.u16PinAttr = PIN_ATTR_DIGITAL;

    (void)GPIO_Init(DRV8301_FAULT_PORT, DRV8301_FAULT_PIN, &stcGpioInit);
    (void)GPIO_Init(DRV8301_NOCTW_PORT, DRV8301_NOCTW_PIN, &stcGpioInit);
    (void)GPIO_Init(DRV8301_PWRGD_PORT, DRV8301_PWRGD_PIN, &stcGpioInit);

    /* ---- 数字输入: DRV8301 状态/故障 + 按键 EIRQ (PFSR.INTE) ----
       EIRQx 的输入经 GPIO 复用得到 (PD2/PB3/PB4 分别对应 EIRQ2/3/4),
       因此引脚要配 PIN_EXTINT_ON。注意: EIRQ3 与霍尔 C 共用 PA3
       (第二功能 C), PA3 同时使能了外部中断, 需注意两者共用。 */
    stcGpioInit.u16ExtInt = PIN_EXTINT_ON;
    (void)GPIO_Init(KEY_1_PORT, KEY_1_PIN, &stcGpioInit);
    (void)GPIO_Init(KEY_2_PORT, KEY_2_PIN, &stcGpioInit);
    (void)GPIO_Init(KEY_3_PORT, KEY_3_PIN, &stcGpioInit);

    /* 霍尔输入: 数字输入并上拉 (原工程用外部上拉 10k);
       Tmra2CaptInit() 会把它们复用为 TMRA_2 捕获功能, 这里先清掉
       u16ExtInt (避免 PA3 既做捕获又做 EIRQ3, 同时 KEY2 也用到) */
    stcGpioInit.u16ExtInt = 0U;
    (void)GPIO_Init(HALL_A_PORT, HALL_A_PIN, &stcGpioInit);
    (void)GPIO_Init(HALL_B_PORT, HALL_B_PIN, &stcGpioInit);
    (void)GPIO_Init(HALL_C_PORT, HALL_C_PIN, &stcGpioInit);

    /* ---- SPI2 复用配置 (配置 SCK/MISO/MOSI, 具体由 SPI2 外设接管) ---- */
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
 * @brief  SPI2 主机模式 16 位数据, 模式 1 (CPOL=0, CPHA=1), MSB 先行
 * @param  [in] u32Baud  DRV8301_SPI_BR_x (PCLK1 = 100MHz)
 */
static void Spi2Init(uint32_t u32Baud)
{
    stc_spi_init_t stcSpiInit;

    FCG_Fcg1PeriphClockCmd(FCG1_PERIPH_SPI2, ENABLE);
    SPI_Cmd(DRV8301_SPI_UNIT, DISABLE);

    (void)SPI_StructInit(&stcSpiInit);
    stcSpiInit.u32WireMode          = SPI_4_WIRE;    /* NSS 由软件控制; 本工程用 GPIO 片选 */
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
 * @brief  传输 16 位数据 (供 DRV8301 读写命令)。
 * @retval 读回的 16 位数据; 超时返回 0。
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
 * @brief  每次 16 位传输之间给 nSCS 留出建立时间 (满足 DRV8301 的 tCSH)。
 */
static void SpiDelay(void)
{
    DDL_DelayUS(1UL);
}

/**
 * @brief  DRV8301 写寄存器 (addr[12:11] | data[10:0])。
 * @retval 始终返回 1 (写命令不校验)。
 */
uint8_t Drv8301Write(uint8_t u8Addr, uint16_t u16Data)
{
    uint16_t u16Frame;

    u16Frame = (uint16_t)(((uint16_t)(u8Addr & 0x03U) << 11U) | (u16Data & 0x07FFU));

    DRV_CS_LOW();
    SpiDelay();                 /* 拉起 SCLK 前先等待 nSCS 建立时间 */
    (void)Spi2Transfer16(u16Frame);
    DRV_CS_HIGH();
    SpiDelay();

    return 1U;
}

/**
 * @brief  DRV8301 读寄存器: 先发地址帧, 再收数据帧。
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

    /* 读操作: 拉低 nSCS 后发起第二帧读数据 */
    DRV_CS_LOW();
    SpiDelay();
    u16Data = Spi2Transfer16(0x0000U);
    DRV_CS_HIGH();
    SpiDelay();

    return u16Data;
}

/* ============================== ADC ====================================== */

/**
 * @brief  ADC1 序列 A: I_total, VBUS, IA, IB, NTC 共 5 个通道。
 *         原工程: ADC1 注入组 (VBUS/IA/IB/ITOTAL, TIM1_CC4 触发) + DMA。
 *         本工程 (B2): 序列 A 由 TMRA_1 OVF 事件经 AOS 硬件触发, 结果由
 *         DMA1 CH0 从 DR2 起搬 12 个 16-bit 到 g_au16AdcValue, 块完成
 *         中断 (INT038) 里跑 communication_task() (将来 FOC 也放这里)。
 */
static void AdcInit(void)
{
    stc_adc_init_t stcAdcInit;
    stc_gpio_init_t stcGpioInit;

    FCG_Fcg3PeriphClockCmd(FCG3_PERIPH_ADC1, ENABLE);

    /* 模拟输入脚: PC0..PC3 (VBUS/IA/IB/NTC) + PA2 (I_total) */
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

    /* B2: 序列 A 硬件触发 (AOS) + 结果 DMA 搬运 */
    AdcHardTriggerConfig();
    AdcDmaConfig();
}

/**
 * @brief  ADC1 序列 A 硬件触发 (B2): 内部事件 EVT0, 触发源由 AOS 选为
 *         TMRA_1 OVF。与原软件 ADC_Start 同一时刻 (PWM 周期点), 但由硬件
 *         精确触发, 不占 CPU。
 */
static void AdcHardTriggerConfig(void)
{
    /* AOS 时钟: ADC1 的 EVT0 触发源由 AOS_TRGSEL 选择 */
    FCG_Fcg0PeriphClockCmd(FCG0_PERIPH_AOS, ENABLE);

    ADC_TriggerConfig(CM_ADC1, ADC_SEQ_A, ADC_HARDTRIG_EVT0);
    AOS_SetTriggerEventSrc(ADC_HWTRIG_AOS_SEL, ADC_HWTRIG_EVT);
    ADC_TriggerCmd(CM_ADC1, ADC_SEQ_A, ENABLE);
}

/**
 * @brief  ADC 结果 DMA (B2): ADC1_EOCA 事件经 AOS 触发 DMA1 CH0, 每次搬一块
 *         (DR2 起 ADC_DMA_BLOCK_SIZE 个 16-bit); 源地址递增, 每次搬完由重复
 *         模式回卷; 块完成中断里跑 communication_task()。
 */
static void AdcDmaConfig(void)
{
    stc_dma_init_t stcDmaInit;
    stc_dma_repeat_init_t stcDmaRptInit;
    stc_irq_signin_config_t stcIrq;

    (void)DMA_StructInit(&stcDmaInit);
    stcDmaInit.u32IntEn       = DMA_INT_ENABLE;
    stcDmaInit.u32SrcAddr     = ADC_DMA_SRC_ADDR;
    stcDmaInit.u32DestAddr    = (uint32_t)(&g_au16AdcValue[0]);
    stcDmaInit.u32DataWidth   = ADC_DMA_DATA_WIDTH;
    stcDmaInit.u32BlockSize   = ADC_DMA_BLOCK_SIZE;
    stcDmaInit.u32TransCount  = 0UL;                    /* 0: 一直搬运 */
    stcDmaInit.u32SrcAddrInc  = DMA_SRC_ADDR_INC;
    stcDmaInit.u32DestAddrInc = DMA_DEST_ADDR_INC;

    FCG_Fcg0PeriphClockCmd(FCG0_PERIPH_DMA1, ENABLE);
    (void)DMA_Init(ADC_DMA_UNIT, ADC_DMA_CH, &stcDmaInit);

    stcDmaRptInit.u32Mode      = DMA_RPT_BOTH;
    stcDmaRptInit.u32SrcCount  = ADC_DMA_BLOCK_SIZE;
    stcDmaRptInit.u32DestCount = ADC_DMA_BLOCK_SIZE;
    (void)DMA_RepeatInit(ADC_DMA_UNIT, ADC_DMA_CH, &stcDmaRptInit);

    /* AOS: ADC1 序列 A 结束事件 -> DMA1 CH0 */
    AOS_SetTriggerEventSrc(ADC_DMA_AOS_TRIG_SEL, ADC_DMA_TRIG_EVT);

    /* 块完成中断: 每搬完一块进一次 (20kHz), 将来 FOC 也放这里 */
    stcIrq.enIntSrc    = ADC_DMA_INT_SRC;
    stcIrq.enIRQn      = ADC_DMA_INT_IRQn;
    stcIrq.pfnCallback = &AdcDmaIrqCallback;
    IntcIrqInstall(&stcIrq, ADC_DMA_INT_PRIO);
    DMA_ClearTransCompleteStatus(ADC_DMA_UNIT, ADC_DMA_INT_FLAG);

    DMA_Cmd(ADC_DMA_UNIT, ENABLE);
    DMA_ChCmd(ADC_DMA_UNIT, ADC_DMA_CH, ENABLE);
}

/**
 * @brief  DMA 块完成中断 (INT038): 结果已在 g_au16AdcValue。
 *         原工程在 ADC 中断里 读结果 -> FOC -> communication_task;
 *         此处先只跑 PC 链路采样, 接 FOC 后把算法加在 communication_task 之前。
 */
static void AdcDmaIrqCallback(void)
{
    DMA_ClearTransCompleteStatus(ADC_DMA_UNIT, ADC_DMA_INT_FLAG);
    pc_test_sine_task();      /* 正弦测试信号, 每个采样节拍更新一次 */
    communication_task();
}

/* ============================== PWM ====================================== */

/**
 * @brief  20kHz TMRA_1 溢出中断: 只做 1ms 分频 (ADC 采样已改硬件触发)。
 *
 * 原工程: ADC 由 TIM1_CC4 触发, 转换完成 JEOC 中断里跑 motor_run()。
 * 本工程 (B2): ADC 序列 A 由 TMRA_1 OVF 事件经 AOS 硬件触发, 结果 DMA
 * 搬运, 在 DMA 块完成中断里跑 communication_task(); 本中断只保留
 * 1ms 软分频 (对应原工程的 SysTick)。
 */
static void Tmra1OvfIrqCallback(void)
{
    TMRA_ClearStatus(CM_TMRA_1, TMRA_FLAG_OVF);

    /* ADC 采样已改由 TMRA_1 OVF 事件经 AOS 硬件触发 + DMA 搬运, 见 AdcInit;
       communication_task() 挪到 DMA 块完成中断 (AdcDmaIrqCallback)。 */

    if (++s_u32PwmCnt >= (PWM_TIM_FREQ / 1000UL)) {
        s_u32PwmCnt = 0UL;
        g_u8Tick1ms = 1U;           /* 置 1ms 标志 */
    }
}

/**
 * @brief  安装中断: 注册回调 (INTC 注册 + NVIC 使能)。
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
 * @brief  TMRA_1 互补 PWM, 20kHz, 初始占空比 50% (对齐中心)。
 *         原工程: TIM1 互补 10kHz PWM + BDTR 死区; 本工程 DRV8301
 *         接 3-PWM 模式 (INL 选择脚控制, 由硬件插入死区)。
 *
 * EN_GATE 未使能时无输出 (默认不使能), 因此加注释说明本工程 PWM
 * 输出到 DRV8301 的前提条件。
 */
static void PwmInit(void)
{
    stc_tmra_init_t stcTmraInit;
    stc_tmra_pwm_init_t stcPwmInit;
    stc_irq_signin_config_t stcIrqSigninConfig;

    FCG_Fcg2PeriphClockCmd(FCG2_PERIPH_TMRA_1, ENABLE);

    /* PA8/PA9/PA10 -> TIMA_1_PWM1/2/3, 复用功能 4 */
    GPIO_SetFunc(PWM_AH_PORT, PWM_AH_PIN, PWM_GPIO_FUNC);
    GPIO_SetFunc(PWM_BH_PORT, PWM_BH_PIN, PWM_GPIO_FUNC);
    GPIO_SetFunc(PWM_CH_PORT, PWM_CH_PIN, PWM_GPIO_FUNC);

    (void)TMRA_StructInit(&stcTmraInit);
    stcTmraInit.sw_count.u8ClockDiv  = TMRA_CLK_DIV1;
    stcTmraInit.sw_count.u8CountMode = TMRA_MD_TRIANGLE;    /* 三角波载波 (对应 STM32 的 TIM_CounterMode_CenterAligned1) */
    stcTmraInit.sw_count.u8CountDir  = TMRA_DIR_UP;
    stcTmraInit.u32PeriodValue       = PWM_TIM_PULSE;
    (void)TMRA_Init(CM_TMRA_1, &stcTmraInit);

    (void)TMRA_PWM_StructInit(&stcPwmInit);
    stcPwmInit.u32CompareValue         = PWM_TIM_PULSE / 2UL;   /* 50% 占空比 */
    stcPwmInit.u16StartPolarity        = TMRA_PWM_HIGH;
    stcPwmInit.u16StopPolarity         = TMRA_PWM_LOW;
    stcPwmInit.u16CompareMatchPolarity = TMRA_PWM_INVT;
    stcPwmInit.u16PeriodMatchPolarity  = TMRA_PWM_HOLD;

    (void)TMRA_PWM_Init(CM_TMRA_1, TMRA_CH1, &stcPwmInit);
    (void)TMRA_PWM_Init(CM_TMRA_1, TMRA_CH2, &stcPwmInit);
    (void)TMRA_PWM_Init(CM_TMRA_1, TMRA_CH3, &stcPwmInit);

    /* 20kHz 中断服务 = ADC 采样触发 + 1ms 分频
       (原工程: NVIC ADC_IRQn 优先级为 1, 此处复用为 PWM 中断) */
    stcIrqSigninConfig.enIRQn      = INT080_IRQn;
    stcIrqSigninConfig.enIntSrc    = INT_SRC_TMRA_1_OVF;
    stcIrqSigninConfig.pfnCallback = &Tmra1OvfIrqCallback;
    IntcIrqInstall(&stcIrqSigninConfig, DDL_IRQ_PRIO_03);

    TMRA_IntCmd(CM_TMRA_1, TMRA_INT_OVF, ENABLE);

    TMRA_SetCountValue(CM_TMRA_1, 1UL);
    TMRA_Start(CM_TMRA_1);
}

/* ========================= 霍尔捕获 (TMRA_2) ============================= */

/**
 * @brief  TMRA_2 溢出中断: 16 位捕获计数的高位扩展。
 *
 * 原工程 TIM2 为 32 位计数: HC32 TMRA 只有 16 位 (PCLK1/16 = 6.25MHz,
 * 溢出周期 10.49ms), 用溢出次数扩展成 32 位, 供边沿间隔计算。
 */
static void Tmra2OvfIrqCallback(void)
{
    TMRA_ClearStatus(HALL_CAPT_UNIT, TMRA_FLAG_OVF);
    s_u32HallOvfCnt++;
}

/**
 * @brief  TMRA_2 捕获中断 (INT_SRC_TMRA_2_CMP, 三通道共用一个回调)。
 *
 * 处理流程: 判断哪个通道触发 -> 组装 32 位时间戳 ->
 * 计算与上次边沿的间隔 (捕获值 + 溢出高位) ->
 * 读取霍尔扇区 (替代原 TIM2_IRQHandler 里读 GPIO 的做法)。
 * 首个边沿不计间隔, 只记录时间戳; 后续边沿才更新间隔。
 * 扇区电平由 Board_HallRead() 读取, 供换相使用。
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
                u32Ch = s_au32HallCh[i];    /* 记录首个触发通道 */
                u8EdgeSeen = 1U;
            }
        }
    }
    if (u8EdgeSeen == 0U) {
        return;                             /* 无有效边沿, 直接返回 */
    }

    u16Cap = (uint16_t)TMRA_GetCompareValue(HALL_CAPT_UNIT, u32Ch);
    u32Now = (s_u32HallOvfCnt << 16) | (uint32_t)u16Cap;
    /* 时间戳组装: 若捕获值较小但已发生 OVF 溢出, 则补上一个溢出周期
       0x10000, 避免跨溢出时计算出的间隔为负 */
    if ((TMRA_GetStatus(HALL_CAPT_UNIT, TMRA_FLAG_OVF) == SET) && (u16Cap < 0x8000U)) {
        u32Now += 0x10000UL;
    }

    if (s_u8HallEdgeValid != 0U) {
        g_u32HallEdgeInterval = u32Now - s_u32HallLastEdge;
    } else {
        g_u32HallEdgeInterval = 0U;         /* 首个边沿无间隔可言 */
        s_u8HallEdgeValid = 1U;
    }
    s_u32HallLastEdge = u32Now;
    g_u32HallEdgeCnt++;

    /* 扇区读取: 直接读三个霍尔输入的电平 (HC32 的捕获通道不反映电平),
       用 Func5 复用后的输入 (也即捕获所用引脚) */
    g_u8HallSector = Board_HallRead();
}

/**
 * @brief  TMRA_2 捕获初始化, 对应原 TIM2 的霍尔接口。
 *
 *   原工程 (STM32 TIM2)              本工程 (TMRA_2)
 *   -----------------------          -----------------------------
 *   3 通道输入 (TI1S) 复用          捕获: 三通道双沿捕获
 *                                     PWM 输入, 6 路可独立配置
 *   TI1 捕获                        HICP0|HICP1 捕获 (双沿)
 *                                     只用其一即可
 *   溢出计数, CCR1 = 周期          比较值捕获, 溢出高位扩展
 *   32 位计数 @90MHz               16 位 @6.25MHz + 溢出扩展
 *
 * 注意: PA0/PA1/PA3 -> Func5 (TIMA_2_PWM1/2/4), 复用配置见 GpioInit()
 * 说明, GPIO_SetFunc 设置 FSEL, 与捕获功能一致。
 */
static void Tmra2CaptInit(void)
{
    stc_tmra_init_t stcTmraInit;
    stc_irq_signin_config_t stcIrqSigninConfig;
    uint8_t i;

    HALL_CAPT_FCG_ENABLE();

    /* 配置引脚复用为 TMRA_2 捕获功能 (见 GpioInit) */
    GPIO_SetFunc(HALL_A_PORT, HALL_A_PIN, HALL_CAPT_GPIO_FUNC);
    GPIO_SetFunc(HALL_B_PORT, HALL_B_PIN, HALL_CAPT_GPIO_FUNC);
    GPIO_SetFunc(HALL_C_PORT, HALL_C_PIN, HALL_CAPT_GPIO_FUNC);

    /* 捕获定时器计数: 向上计数, 周期 0xFFFF (6.25MHz -> 10.49ms) */
    (void)TMRA_StructInit(&stcTmraInit);
    stcTmraInit.sw_count.u8ClockDiv  = HALL_CAPT_CLK_DIV;
    stcTmraInit.sw_count.u8CountMode = TMRA_MD_SAWTOOTH;
    stcTmraInit.sw_count.u8CountDir  = TMRA_DIR_UP;
    stcTmraInit.u32PeriodValue       = 0xFFFFUL;
    (void)TMRA_Init(HALL_CAPT_UNIT, &stcTmraInit);

    /* 配置捕获: 上升沿 + 下降沿 (双沿捕获) */
    for (i = 0U; i < 3U; i++) {
        TMRA_SetFunc(HALL_CAPT_UNIT, s_au32HallCh[i], TMRA_FUNC_CAPT);
        TMRA_HWCaptureCondCmd(HALL_CAPT_UNIT, s_au32HallCh[i], HALL_CAPT_COND_BOTH, ENABLE);
    }

    /* 使能中断 (溢出) + 捕获中断, 优先级 DDL_IRQ_PRIO_03, 与 TMRA_1 OVF
       同级: 捕获对实时性要求高, 需及时记录时间戳供 FOC 使用 */
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

/* ========================= 按键 EXTI (EIRQ) ============================== */

/**
 * @brief  按键 1 回调 (KEY1 = PD2 = EIRQ2, 下降沿)。
 *
 * 中断里只置位标志: 置 key1_flag 并开始消抖计数, 由 Board_Key1msTask()
 * 轮询到消抖结束后重新使能中断 (避免抖动期间反复进中断)。
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
 * @brief  按键 EXTI 初始化 (原工程: SYSCFG_EXTILineConfig + EXTI_Init
 *         配置 + NVIC 使能; 本工程: GPIO PFSR.INTE + EXTINT_Init +
 *         INTC 注册)。滤波后 (分频 /8) 用数字滤波, ms 级去抖,
 *         优先级 DDL_IRQ_PRIO_04: 低于电机控制, 不与 PWM/捕获冲突
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
 * @brief  按键扫描/消抖处理, 由 1ms 定时任务调用。
 *
 * 利用 KEY_DEBOUNCE_MS 的计数: 每次 1ms 递增, 达到阈值后
 * 认为按键稳定, 清除标志并重新使能对应中断, 实现一次触发一次响应
 * (软件消抖, 不阻塞中断)。
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

/* ============================ 板级接口 ============================== */

/**
 * @brief  设置三相占空比, 范围 0.0 ~ 1.0。
 *         (对应原工程: FOC 计算后写 PWM_TIM->CCR1/2/3)
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
 * @brief  PWM 输出使能/关闭 (对应 STM32: TIM_CtrlPWMOutputs(PWM_TIM, x))。
 *         默认不使能: EN_GATE 未拉高时不输出到 DRV8301。
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
 * @brief  读取霍尔扇区, bit0 = A, bit1 = B, bit2 = C。
 *         引脚已由 Func5 (TMRA_2 捕获) 复用, 这里直接读输入电平。
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
 * @brief  板级初始化入口, 由 main() 调用。
 *
 * 调用约定: 必须在打开写保护前调用 (main 里 LL_PERIPH_WE(...)，
 * 因为要用到 LL_PERIPH_FCG 和 LL_PERIPH_GPIO)。SystemClockInit() 里
 * 用到 DDL_DelayMS/US 延时。
 * USB 初始化 (usb_dev_init, 见 usb_bsp 引脚/时钟/端点 + CDC 驱动) 在
 * main 里 hard_init() 之后完成。
 */
void hard_init(void)
{
    SystemClockInit();                  /* 时钟: 系统时钟 + 外设时钟 (含 USB PLLX) */
    GpioInit();                         /* GPIO: DRV8301 / OLED / LED / 按键 / 霍尔引脚 */
    Spi2Init(DRV8301_SPI_BR_FAST);      /* SPI: 初始化 SPI2, 供 drv8301_init() 使用 */
    AdcInit();                          /* ADC: ADC 初始化 */
    PwmInit();                          /* PWM: TMRA_1 PWM + NVIC 中断 */
    Tmra2CaptInit();                    /* TMRA_2: 霍尔捕获 */
    KeyExtiInit();                      /* 按键: 外部 EXTI + NVIC 配置 */

    /*
     * 初始保持 EN_GATE 为低 (不使能功率级)。
     真正使能需在完成 drv8301_init() 的 SPI 配置之后;
     DRV8301 上电不会自动输出 (见 drv8301_test 的用法)。
     */
    EN_GATE_LOW();
    DC_CAL_LOW();
    USER_LED_OFF();
}

/*******************************************************************************
 * EOF (not truncated)
 ******************************************************************************/
