/**
 *******************************************************************************
 * @file  foc/source/board_config.h
 * @brief HC32F460KETA + DRV8301 电机驱动板 - 板级引脚映射与时序参数定义
 *
 * 移植自 STM32F4 配套工程:
 *   配套代码/代码/code/user/board_config.h + board_config.c (hard_init)
 * 宏命名尽量沿用 STM32 侧的原名, 便于后续移植 foc_algorithm / low_task 时
 * 直接复用; 宏值全部按本板网表 (硬件/Netlist_Schematic1_2026-09-11.net)
 * 以及 drv8301_test / usb_dev_cdc 已在板上验证过的映射填写。
 *
 * 与 STM32 配套板的主要差异 (详见 board_config.c):
 *   - PWM  : 源板用 TIM1 互补 6 路 PWM + BDTR 硬件死区; 本板 DRV8301 配置为
 *            3-PWM 模式 (INL 拉低, 由 DRV8301 内部插入死区), MCU 只输出
 *            三路高侧 PWM (TMRA_1)。
 *   - ADC  : 源板用 ADC1 注入组 (TIM1_CC4 触发) + 规则组 DMA; HC32F460 的
 *            ADC1 没有 DMA, 改为 20kHz PWM 溢出中断在三角波顶点软件启动
 *            序列 A, 采样点与源方案一致 (下管导通窗口, 相电流有效)。
 *   - 霍尔  : 源板用 TIM2 霍尔接口 (TI1 双边沿 + 从模式复位) 测周期, 扇区
 *            位置本来也是 GPIO 读的 (见源 hall_sensor.c 的 TIM2_IRQHandler)。
 *            本板: 扇区位置用 GPIO 读 (Board_HallRead), 周期测量用 TMRA_2
 *            三通道 (PA0/PA1/PA3 = CH1/CH2/CH4, 数据手册表 2-1) 硬件双沿
 *            捕获 + 溢出软件扩展 + 软件做差, 6 沿/电周期与源方案等价。
 *   - 按键  : 与源工程同为 EXTI 下降沿中断 (PD2/PB3/PB4 = EIRQ2/3/4)。
 *            EIRQx 按引脚号在所有端口共享, 由引脚 PFSR 的 EXTINT 位决定
 *            接入者: 只给按键引脚置 PIN_EXTINT_ON (PA3 是霍尔, 不开)。
 *            ms 级抖动用 "屏蔽中断 + 1ms 任务计时后重武装" 消除,
 *            按下产生 keyN 标志, 语义与源工程 key1_flag/key2_flag/key3_flag
 *            完全一致 (置 1, low_task 消费后清零)。
 *   - USB   : 源板 PC 通信走 USB FS。本板 Type-C (USB1) 的 DP/DM 接
 *            PA12/PA11, 但 VBUS 不接 MCU (PA9 = PWM_BH), 所以 USB 以
 *            "无 VBUS 检测" 模式运行 (usb_app_conf.h 不定义
 *            VBUS_SENSING_ENABLED, usb_bsp.c 强制 VBUS 有效)。
 *            USB 需要 PLLX 输出精确 48MHz, 与主 PLL 相互独立。
 *   - OLED  : 源板 OLED 为软件 SPI (PB3/PB5 + PC13/PC8)。本板 P6 排针为
 *            I2C 接口, OLED_SCL=PC7 / OLED_SDA=PC8 (10k 上拉 R95/R96)。
 *            源工程的 SPI OLED 驱动不适用于本板, 暂未移植 OLED 驱动。
 *******************************************************************************
 */
#ifndef __BOARD_CONFIG_H__
#define __BOARD_CONFIG_H__

#include "main.h"

/*******************************************************************************
 * 时钟 / 时基
 ******************************************************************************/
/* SystemClockInit() 之后的时钟树: HCLK 200MHz, PCLK1 100MHz, PCLK2 50MHz
   (板载晶振 X3 为 16MHz)。 */
#define HCLK_FREQ                   (200000000UL)
#define PCLK1_FREQ                  (100000000UL)

/* USB FS 时钟 (PLLX/PLLXP), 与主 PLL 相互独立, 必须精确 48MHz:
   16MHz / 4 = 4MHz (PLLX 输入, 1..25MHz), 4MHz * 84 = 336MHz (VCO,
   240..480MHz), 336MHz / 7 = 48MHz。主 PLL 的 200MHz / PCLK1 100MHz 不受影响。 */
#define USB_PLL_M                   (4UL)
#define USB_PLL_N                   (84UL)
#define USB_PLL_P                   (7UL)

/* 源工程: PWM_TIM_FREQ = 10000Hz, FOC_PERIOD = 0.0001s。
   本板按 drv8301_test 已验证的 20kHz 运行; 两者必须保持一致:
   FOC_PERIOD = 1 / PWM_TIM_FREQ。移植 foc_algorithm 时所有时间相关的
   常量都应从 PWM_TIM_FREQ 推导。 */
#define PWM_TIM_FREQ                (20000UL)
#define PWM_TIM_CLOCK               PCLK1_FREQ                              /* TMRA 计数时钟 */
#define PWM_TIM_PULSE               (PWM_TIM_CLOCK / (2UL * PWM_TIM_FREQ))  /* 2500 */
/* FOC 算法使用的调制周期, 单位秒 (对应源工程 FOC_PERIOD)。 */
#define FOC_PERIOD                  (1.0f / (float)PWM_TIM_FREQ)

/*******************************************************************************
 * 功率级 PWM (源: PWM_TIM / TIM1, PA8/PA9/PA10 + PB13/14/15)
 * HC32F460: TMRA_1, 六个引脚全部为功能 4 (GPIO_FUNC_4)。
 ******************************************************************************/
#define PWM_AH_PORT                 (GPIO_PORT_A)
#define PWM_AH_PIN                  (GPIO_PIN_08)       /* 对应 STM32: PA8  TIM1_CH1 */
#define PWM_BH_PORT                 (GPIO_PORT_A)
#define PWM_BH_PIN                  (GPIO_PIN_09)       /* 对应 STM32: PA9  TIM1_CH2; 本板 PA9 未接 USB VBUS */
#define PWM_CH_PORT                 (GPIO_PORT_A)
#define PWM_CH_PIN                  (GPIO_PIN_10)       /* 对应 STM32: PA10 TIM1_CH3; 本板 PA10 未接 USB ID */
/* 低侧 INH 引脚固定拉低: DRV8301 3-PWM 模式下为无关脚。 */
#define PWM_INL_PORT                (GPIO_PORT_B)
#define PWM_INL_PIN                 (GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15)
#define PWM_GPIO_FUNC               (GPIO_FUNC_4)

/*******************************************************************************
 * ADC (源: ADC1 + DMA2_Stream0, 注入组由 TIM1_CC4 触发)
 * HC32F460: ADC1 序列 A, 由 PWM 中断软件启动。
 ******************************************************************************/
/* 低侧采样电阻 20mR (R40/R48), DRV8301 增益 10V/V:
   1 个 ADC 码 = 3.3V / 4095 / (0.020 * 10) = 4.028mA。 */
#define SHUNT_OHM                   (0.020f)
#define AMP_GAIN                    (10.0f)
#define ADC_VREF                    (3.3f)
#define ADC_FULL_SCALE              (4095.0f)

/* 母线电压分压比: 95.3k / 4.99k */
#define VBUS_DIV                    (0.049756f)

/* 母线总电流: U9 TP181A1 (增益 50V/V) 跨接 R57 = 5mR。
   Vout = 1.65V - I * 0.25V/A (电流为正时输出下降)。 */
#define ITOTAL_SHUNT_OHM            (0.005f)
#define ITOTAL_GAIN                 (50.0f)

/* NTC: 25 度时 10k, 4.7k 上拉到 3.3V, Beta = 3950 */
#define NTC_PULLUP_OHM              (4700.0f)
#define NTC_R25_OHM                 (10000.0f)
#define NTC_BETA                    (3950.0f)

#define VBUS_ADC_PORT               (GPIO_PORT_C)
#define VBUS_ADC_PIN                (GPIO_PIN_00)       /* 对应 STM32: PC0, ADC1_IN10 */
#define IA_CURRENT_ADC_PORT         (GPIO_PORT_C)
#define IA_CURRENT_ADC_PIN          (GPIO_PIN_01)       /* 对应 STM32: PC1, ADC1_IN11 */
#define IB_CURRENT_ADC_PORT         (GPIO_PORT_C)
#define IB_CURRENT_ADC_PIN          (GPIO_PIN_02)       /* 对应 STM32: PC2, ADC1_IN12 */
#define NTC_ADC_PORT                (GPIO_PORT_C)
#define NTC_ADC_PIN                 (GPIO_PIN_03)       /* 对应 STM32: PC3, ADC1_IN13 */
#define ITOTAL_ADC_PORT             (GPIO_PORT_A)
#define ITOTAL_ADC_PIN              (GPIO_PIN_02)       /* 源板在 PA6, 本板为 PA2 */

#define ADC_VBUS_CH                 (ADC_CH10)
#define ADC_IA_CH                   (ADC_CH11)
#define ADC_IB_CH                   (ADC_CH12)
#define ADC_NTC_CH                  (ADC_CH13)
#define ADC_ITOTAL_CH               (ADC_CH2)

#define ADC_SAMPLE_TIME             (0x20U)

/*******************************************************************************
 * DRV8301 (引脚名与 STM32 板一致)
 ******************************************************************************/
#define DRV8301_ENGATE_PORT         (GPIO_PORT_B)
#define DRV8301_ENGATE_PIN          (GPIO_PIN_09)
#define DRV8301_DCCAL_PORT          (GPIO_PORT_B)       /* STM32 板无此脚 */
#define DRV8301_DCCAL_PIN           (GPIO_PIN_08)
#define DRV8301_FAULT_PORT          (GPIO_PORT_B)       /* STM32 板在 PD2 */
#define DRV8301_FAULT_PIN           (GPIO_PIN_02)
#define DRV8301_NOCTW_PORT          (GPIO_PORT_C)       /* STM32 板无此脚 */
#define DRV8301_NOCTW_PIN           (GPIO_PIN_06)
#define DRV8301_PWRGD_PORT          (GPIO_PORT_B)       /* STM32 板无此脚 */
#define DRV8301_PWRGD_PIN           (GPIO_PIN_05)

/* DRV8301 SPI: 硬件 SPI2 (PC10 SCK / PC11 MISO / PC12 MOSI, 功能组 1),
   nSCS 保持普通 GPIO (PA15), 由软件逐帧拉低/拉高。
   源板同样为 PC10/11/12 + PA15, 但全部按位带 GPIO 方式驱动。 */
#define DRV8301_SPIx_CS_PORT        (GPIO_PORT_A)
#define DRV8301_SPIx_CS_PIN         (GPIO_PIN_15)
#define DRV8301_SPIx_SCK_PORT       (GPIO_PORT_C)
#define DRV8301_SPIx_SCK_PIN        (GPIO_PIN_10)
#define DRV8301_SPIx_SCK_FUNC       (GPIO_FUNC_47)
#define DRV8301_SPIx_MISO_PORT      (GPIO_PORT_C)
#define DRV8301_SPIx_MISO_PIN       (GPIO_PIN_11)
#define DRV8301_SPIx_MISO_FUNC      (GPIO_FUNC_45)
#define DRV8301_SPIx_MOSI_PORT      (GPIO_PORT_C)
#define DRV8301_SPIx_MOSI_PIN       (GPIO_PIN_12)
#define DRV8301_SPIx_MOSI_FUNC      (GPIO_FUNC_44)
#define DRV8301_SPI_UNIT            (CM_SPI2)

#define DRV8301_SPI_BR_FAST         (SPI_BR_CLK_DIV64)      /* 100MHz / 64  = 1.5625MHz */
#define DRV8301_SPI_BR_SLOW         (SPI_BR_CLK_DIV256)     /* 100MHz / 256 = 390kHz, 排查用 */
#define DRV8301_SPI_TIMEOUT         (0x200000UL)

#define EN_GATE_LOW()               GPIO_ResetPins(DRV8301_ENGATE_PORT, DRV8301_ENGATE_PIN)
#define EN_GATE_HIGH()              GPIO_SetPins(DRV8301_ENGATE_PORT, DRV8301_ENGATE_PIN)
#define DC_CAL_LOW()                GPIO_ResetPins(DRV8301_DCCAL_PORT, DRV8301_DCCAL_PIN)
#define DC_CAL_HIGH()               GPIO_SetPins(DRV8301_DCCAL_PORT, DRV8301_DCCAL_PIN)
#define NFAULT_LOW()                (GPIO_ReadInputPins(DRV8301_FAULT_PORT, DRV8301_FAULT_PIN) == PIN_RESET)
#define NOCTW_LOW()                 (GPIO_ReadInputPins(DRV8301_NOCTW_PORT, DRV8301_NOCTW_PIN) == PIN_RESET)
#define DRV_CS_LOW()                GPIO_ResetPins(DRV8301_SPIx_CS_PORT, DRV8301_SPIx_CS_PIN)
#define DRV_CS_HIGH()               GPIO_SetPins(DRV8301_SPIx_CS_PORT, DRV8301_SPIx_CS_PIN)

/*******************************************************************************
 * USB FS (源工程 PC 通信走 USB; 本板 Type-C, DP=PA12 / DM=PA11, 无 VBUS 检测)
 * 引脚/时钟/中断的配置在 usb_bsp.c (从 usb_dev_cdc 工程移植)。
 ******************************************************************************/
#define USB_DM_PORT                 (GPIO_PORT_A)
#define USB_DM_PIN                  (GPIO_PIN_11)
#define USB_DP_PORT                 (GPIO_PORT_A)
#define USB_DP_PIN                  (GPIO_PIN_12)

/*******************************************************************************
 * 霍尔传感器 (源: TIM2 CH1/2/3 霍尔接口)
 * 本板方案:
 *   - 位置 (扇区): 三根线读输入电平, Board_HallRead() / g_u8HallSector;
 *   - 周期 (转速/插值): TMRA_2 三通道硬件双沿捕获。
 *     数据手册 Rev1.61 表 2-1: PA0=TIMA_2_PWM1(CH1), PA1=TIMA_2_PWM2(CH2),
 *     PA3=TIMA_2_PWM4(CH4), 全部在功能 5。注意 PA2 才是 TIMA_2_PWM3,
 *     它已被 TP181 总电流采样占用 - 幸好 CH3 不参与, 三个通道互不相干。
 *     HICP 位是独立使能 (或语义), 上升沿+下降沿同时置位 = 硬件双沿捕获,
 *     不需要"异或门+软件翻极性"的替代方案。
 ******************************************************************************/
#define HALL_A_PORT                 (GPIO_PORT_A)
#define HALL_A_PIN                  (GPIO_PIN_00)       /* TMRA_2 CH1, Func5 */
#define HALL_B_PORT                 (GPIO_PORT_A)
#define HALL_B_PIN                  (GPIO_PIN_01)       /* TMRA_2 CH2, Func5 */
#define HALL_C_PORT                 (GPIO_PORT_A)
#define HALL_C_PIN                  (GPIO_PIN_03)       /* TMRA_2 CH4, Func5 */

#define HALL_CAPT_UNIT              (CM_TMRA_2)
#define HALL_CAPT_FCG_ENABLE()      (FCG_Fcg2PeriphClockCmd(FCG2_PERIPH_TMRA_2, ENABLE))
#define HALL_CAPT_GPIO_FUNC         (GPIO_FUNC_5)
#define HALL_CAPT_CH_A              (TMRA_CH1)
#define HALL_CAPT_CH_B              (TMRA_CH2)
#define HALL_CAPT_CH_C              (TMRA_CH4)
#define HALL_CAPT_COND_BOTH         (TMRA_CAPT_COND_PWM_RISING | TMRA_CAPT_COND_PWM_FALLING)
/* 计数时钟 = PCLK1/16 = 6.25MHz, 16 位回绕周期 = 10.49ms。
   溢出中断在软件里扩展高位, 所以低速量程不受 16 位限制 (同源 TIM2 32 位)。 */
#define HALL_CAPT_CLK_DIV           (TMRA_CLK_DIV16)
#define HALL_CAPT_TICK_FREQ         (PCLK1_FREQ / 16UL)     /* 6250000 Hz */

/* 中断向量号: INT080 (TMRA_1 OVF) / INT081/082 (TMRA_2) / INT083-085 (按键)
   / INT000/001 (USART3) / INT030 (USB, 见 usb_bsp.c)。 */
#define HALL_CAPT_OVF_IRQn          (INT081_IRQn)
#define HALL_CAPT_OVF_INT_SRC       (INT_SRC_TMRA_2_OVF)
#define HALL_CAPT_CMP_IRQn          (INT082_IRQn)
#define HALL_CAPT_CMP_INT_SRC       (INT_SRC_TMRA_2_CMP)    /* 所有通道共享一个事件源 */

/*******************************************************************************
 * USER LED (源: PC9) - 本板低电平点亮。
 ******************************************************************************/
#define USER_LED_PORT               (GPIO_PORT_C)
#define USER_LED_PIN                (GPIO_PIN_09)
#define USER_LED_ON()               GPIO_ResetPins(USER_LED_PORT, USER_LED_PIN)
#define USER_LED_OFF()              GPIO_SetPins(USER_LED_PORT, USER_LED_PIN)

/*******************************************************************************
 * 按键 (源: PC4/PC5/PB2 走 EXTI -> 本板 PD2/PB3/PB4 同为 EXTI 下降沿)
 * EIRQ 通道分配 (EIRQx 按引脚号在所有端口共享, 由引脚 PFSR 的 INTE 位
 * 决定接入者):
 *   EIRQ2 = PD2 (KEY1); PA2/PC2 为模拟脚, 无数字沿, 无干扰。
 *   EIRQ3 = PB3 (KEY2); 注意 PA3 是霍尔 C, 绝不能开 PIN_EXTINT_ON。
 *   EIRQ4 = PB4 (KEY3); PA4 未使用。
 ******************************************************************************/
#define KEY_1_PORT                  (GPIO_PORT_D)
#define KEY_1_PIN                   (GPIO_PIN_02)
#define KEY_1_EXTINT_CH             (EXTINT_CH02)
#define KEY_1_INT_SRC               (INT_SRC_PORT_EIRQ2)
#define KEY_1_IRQn                  (INT083_IRQn)
#define KEY_2_PORT                  (GPIO_PORT_B)
#define KEY_2_PIN                   (GPIO_PIN_03)       /* 需先释放 JTAG TDO */
#define KEY_2_EXTINT_CH             (EXTINT_CH03)
#define KEY_2_INT_SRC               (INT_SRC_PORT_EIRQ3)
#define KEY_2_IRQn                  (INT084_IRQn)
#define KEY_3_PORT                  (GPIO_PORT_B)
#define KEY_3_PIN                   (GPIO_PIN_04)       /* 需先释放 JTAG nTRST */
#define KEY_3_EXTINT_CH             (EXTINT_CH04)
#define KEY_3_INT_SRC               (INT_SRC_PORT_EIRQ4)
#define KEY_3_IRQn                  (INT085_IRQn)

/* 消抖: 硬件滤波 (EIRQCR 采样滤波) 只能滤 ns~us 级窄脉冲, ms 级机械抖动
   仍需软件处理。方案: 下降沿中断里置键标志并屏蔽本键中断, 主循环 1ms
   任务计时 KEY_DEBOUNCE_MS 后清状态标志并重新武装。 */
#define KEY_DEBOUNCE_MS             (30UL)

/*******************************************************************************
 * OLED 排针 P6 (源: 软件 SPI PB3/PB5 + PC13/PC8)
 * 本板: I2C 四线排针, SCL=PC7, SDA=PC8, 10k 上拉 R95/R96。
 * 仅保留定义供参考 - 源工程 SPI OLED 驱动不适用, 待移植 I2C 驱动。
 ******************************************************************************/
#define OLED_SCL_PORT               (GPIO_PORT_C)
#define OLED_SCL_PIN                (GPIO_PIN_07)
#define OLED_SDA_PORT               (GPIO_PORT_C)
#define OLED_SDA_PIN                (GPIO_PIN_08)

/* 晶振 (PH0: XTAL_IN, PH1: XTAL_OUT), 16MHz */
#define XTAL_PORT                   (GPIO_PORT_H)
#define XTAL_PIN                    (GPIO_PIN_00 | GPIO_PIN_01)

/*******************************************************************************
 * 中断共享数据 (ISR 里写, 主循环里读; 对应源工程由 SysTick / JEOC /
 * TIM2 / EXTI 中断驱动的变量)。
 ******************************************************************************/
/* TMRA_1 OVF (20kHz) 产生: */
extern volatile uint8_t g_u8AdcBusy;    /* 1: 序列 A 转换进行中 */
extern volatile uint8_t g_u8Tick1ms;    /* 1ms 时基标志, 主循环清零 */

/* TMRA_2 霍尔捕获产生 (对应源工程 TIM2_IRQHandler 维护的量):
   g_u32HallEdgeInterval = 0 表示还没有有效间隔 (未捕获到两个沿)。 */
extern volatile uint32_t g_u32HallEdgeInterval; /* 相邻两霍尔沿的间隔, 单位 tick (6.25MHz) */
extern volatile uint32_t g_u32HallEdgeCnt;      /* 已捕获的沿总数, 用于判断数据是否还新鲜 */
extern volatile uint8_t  g_u8HallSector;        /* 最新扇区码, bit0/1/2 = 霍尔 A/B/C */

/* 按键 EXTI 产生 (语义与源工程 key1_flag/key2_flag/key3_flag 一致:
   按下降沿置 1, low_task 消费后清零)。 */
extern volatile uint8_t g_u8Key1Flag;
extern volatile uint8_t g_u8Key2Flag;
extern volatile uint8_t g_u8Key3Flag;

/*******************************************************************************
 * 函数声明
 ******************************************************************************/
extern void hard_init(void);
extern void Board_PwmSetDuty(float fA, float fB, float fC);
extern void Board_MotorPwmEnable(void);     /* 对应 STM32: TIM_CtrlPWMOutputs(ENABLE)  */
extern void Board_MotorPwmDisable(void);    /* 对应 STM32: TIM_CtrlPWMOutputs(DISABLE) */
extern uint8_t Board_HallRead(void);
extern void Board_Key1msTask(void);         /* 主循环 1ms 分支调用: 按键消抖计时/重武装 */
extern uint8_t Drv8301Write(uint8_t u8Addr, uint16_t u16Data);
extern uint16_t Drv8301Read(uint8_t u8Addr);

#endif /* __BOARD_CONFIG_H__ */

/*******************************************************************************
 * EOF (not truncated)
 ******************************************************************************/
