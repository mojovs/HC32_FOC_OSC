/**
 *******************************************************************************
 * @file  foc/source/main.c
 * @brief HC32F460KETA + DRV8301 FOC ���� - ������
 *
 * ��ֲ�� STM32F4 ���׹��� main():
 *   ���״���/����/code/user/main.c
 *
 * Դ main():                                ����ֲ:
 *   hard_init();                              hard_init();              �����
 *                                             (ʱ�Ӻ� USB PLLX/GPIO/SPI/
 *                                             ADC/PWM/��������/���� EXTI)
 *   OLED_Init();                              δ��ֲ (���� OLED ����Ϊ I2C
 *                                             PC7/PC8, Դ���������� SPI)
 *   drv8301_init();                           ����ֲ (motor/drv8301.c)
 *   pc_communication_init();                  usb_dev_init() �����
 *                                             (USB CDC ���⴮��, ��ֲ��
 *                                             usb_dev_cdc ����; USART3
 *                                             ����̨ + CDC<->CN1 �� +
 *                                             ���� RX ������һ����λ)
 *   foc_algorithm_initialize();               ����ֲ (motor/foc_*.c)
 *   if (get_offset_flag==0) {                 PWM ������� hard_init ��
 *     get_offset_flag=1;                      PwmInit() ����ʹ�� - EN_GATE
 *     TIM_CtrlPWMOutputs(ENABLE);             ���ֵ� (�ż��ض�), ��
 *   }                                         drv8301_init() ��ֲ���ٿ�
 *   while(1) {                                vcp_task()/USB �¼�/ADC/1ms;
 *     drv8301_protection();                   Ԥ�� drv8301_protection() ��
 *     oled_display_handle();                  oled_display_handle() ���ص�
 *   }
 *
 * ͨ�żܹ� (usb_dev_cdc + usart_uart_int �ϲ�):
 *   - USB CDC ���⴮�� <-> CN1 (USART3) ˫���Ž�;
 *   - board -> PC �������ϱ��� vcp_send() (��ӦԴ���� PC ͨ�ŵ�����);
 *   - CN1 �յ����ֽ�ͬʱ�����ػ�����, UART_ReadByte() ��������ȡ,
 *     �������ı���������� (usart_uart_int ����);
 *   - printf �� USART3 TX (CN1); USB/USART �ж����ȼ����,
 *     �����Ŀ��ƻ��жϿ���ʱ��ռ��
 *
 * hard_init() ��ɺ� LED �� 1Hz ��˸, ��Ϊ��ֲ�ڼ���ϵ�ָʾ��
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
 * ͷ�ļ�
 ******************************************************************************/
#include "main.h"
#include "board_config.h"
#include "cdc_data_process.h"
#include "pc_communication.h"
#include "pc_communication_init.h"
#include <stdio.h>

/**
 * @addtogroup HC32F460_DDL_Examples
 * @{
 */

/**
 * @addtogroup FOC
 * @{
 */

/*******************************************************************************
 * �ֲ��궨��
 ******************************************************************************/
/* ����Ĵ���д����ѡ��
   ע��: LL_PERIPH_FCG ���뱣���ڽ���������, ���� CLK_SetSysClockSrc(PLL)
   ���� PWC_FCG0PC.PRT0 д�������� DDL_AssertHandler() �￨���� */
#define LL_PERIPH_SEL                   (LL_PERIPH_GPIO | LL_PERIPH_FCG | LL_PERIPH_PWC_CLK_RMU | \
                                         LL_PERIPH_EFM | LL_PERIPH_SRAM)

/* д��������: GPIO ���ֽ��� - ����������ѭ���﷭ת PC9 (ͬ usb_dev_cdc)�� */
#define LL_PERIPH_WP_SEL                (LL_PERIPH_EFM | LL_PERIPH_PWC_CLK_RMU | LL_PERIPH_SRAM)

/* LED ��������, ��λ ms */
#define LED_TOGGLE_MS                   (500UL)

/*******************************************************************************
 * ȫ�ֱ���
 ******************************************************************************/
/* USB �豸ʵ�� (usb_dev_init/�жϴ���������ʹ����)�� */
usb_core_instance usb_dev;

/*******************************************************************************
 * �ֲ��������� ('static')
 ******************************************************************************/
static void CompanionModulesInit(void);
static void UsbEventTask(void);

/*******************************************************************************
 * ����ʵ�� - ȫ�� ('extern') ��ֲ� ('static')
 ******************************************************************************/

/**
 * @brief  Դ main() ���ε��õĸ�����ģ���ʼ����
 *         ÿ��ģ�鵥����ֲ; ���ﱣ��Դ���̵ĳ�ʼ��˳��, ������ü��ɡ�
 */
static void CompanionModulesInit(void)
{
    /* PC host tool link: reset the engine and register all channels. */
    pc_communication_init();
    /* OLED_Init();                    - TODO: ���� OLED �� I2C �ӿ� (PC7/PC8),
                                         Դ�������� SPI ����������;
                                         ����ֲ I2C SSD1306 ���� */
    /* drv8301_init();                 - TODO: ��ֲ motor/drv8301.c, ֮��
                                         ���� EN_GATE ʹ�� DRV8301, ����
                                         �������궨 (��Ӧ get_offset_flag) */
    /* pc_communication_init();        - ���� usb_dev_init() �е�: USB CDC
                                         ���⴮�ڼ�Դ���̵� PC ͨ��������,
                                         vcp_send()/UART_ReadByte() �������нӿ� */
    /* foc_algorithm_initialize();     - TODO: ��ֲ motor/foc_algorithm.c */
}

/**
 * @brief  ��ӡ USB ջ�¼� (�� usb_dev_user.c �Ļص����ж�����λ)��
 *         printf ֻ������ѭ�����, ���ܽ� USB �ж� (ͬ usb_dev_cdc)��
 *         ��ӦԴ���� communication_handle() �Ľ�ɫ: PC ͨ��״̬ά����
 */
static void UsbEventTask(void)
{
#if (VCP_BRIDGE_TO_UART != 0U)
    static uint8_t m_au8Greeting[] =
        "\r\nSMotorDrive foc firmware: VCP ready.\r\n"
        "USB CDC <-> CN1 (USART3) bridged; commands pending pc_comm port.\r\n";
#endif
    uint8_t u8Evt = g_u8UsbEvt;

    if (0U == u8Evt) {
        return;
    }
    if (g_u8UsbEvt == u8Evt) {
        g_u8UsbEvt = 0U;
    }

    switch (u8Evt) {
        case USB_EVT_RESET:
            printf(">> USB bus reset\r\n");
            break;
        case USB_EVT_CONFIG:
            printf(">> USB configured, CDC ready (COM port open)\r\n");
#if (VCP_BRIDGE_TO_UART != 0U)
            /* ֱ���� CDC IN �˵���ʺ�, ������ CN1�� */
            vcp_send(m_au8Greeting, (uint32_t)(sizeof(m_au8Greeting) - 1U));
#endif
            break;
        case USB_EVT_SUSPEND:
            printf(">> USB suspend\r\n");
            break;
        case USB_EVT_RESUME:
            printf(">> USB resume\r\n");
            break;
        case USB_EVT_DISCONN:
            printf(">> USB disconnected\r\n");
            break;
        default:
            break;
    }
}

/**
 * @brief  FOC ����������
 * @param  None
 * @retval int32_t return value, if needed
 */
int32_t main(void)
{
    stc_usb_port_identify stcPortIdentify;

    /* ������������Ĵ���д���� */
    LL_PERIPH_WE(LL_PERIPH_SEL);

    /* �弶��ʼ��: ʱ�� 200MHz + USB PLLX 48MHz, GPIO, DRV8301 SPI, ADC,
       20kHz PWM, ��������, ���� EXTI�� */
    hard_init();

    CompanionModulesInit();

    /* USB CDC �豸ջ (��ӦԴ���� pc_communication_init()):
       usb_dev_init() -> usb_bsp_init() (DP/DM ����, USB ʱ��Դ, USART3
       ����̨) -> �ں�/FIFO/�ж� (INT030, ������ȼ�) -> CDC �� vcp_init()
       (USART3 �շ� + ���ػ�����)��������д����֮ǰ���á� */
    stcPortIdentify.u8CoreID = USBFS_CORE_ID;
    usb_dev_init(&usb_dev, &stcPortIdentify, &user_desc, &class_cdc_cbk, &user_cb);

    /* ���ز�������Ĵ���д������
       GPIO/FCG ���ֽ��� (�����Ʒ�ת, ����ʱ���ſذ���ʹ��)�� */
    LL_PERIPH_WP(LL_PERIPH_WP_SEL);

    printf("\r\n========================================\r\n");
    printf(" SMotorDrive foc: board init done\r\n");
    printf(" 200MHz, 20kHz PWM, USB CDC + USART3\r\n");
    printf(" CN1: 2=PB7(RX), 3=PB6(TX), 115200-8-N-1\r\n");
    printf("========================================\r\n");

    for (;;) {
        /* CDC(OUT) -> USART3 TX �����ſ�, ������ (USB -> CN1 ��)�� */
        vcp_task();
        communication_send_task();   /* flush a finished PC host wave frame */

        /* USB ջ�¼��ϱ� (��ӦԴ���� communication_handle ��ռλ)�� */
        UsbEventTask();

        /* ADC ���� A ת�����, �� 20kHz PWM �ж�����:
           TODO ����/����������ֲ���ڴ�����
           (Դ����: drv8301_protection() + FOC ������)�� */
        if (ADC_GetStatus(CM_ADC1, ADC_FLAG_EOCA) == SET) {
            ADC_ClearStatus(CM_ADC1, ADC_FLAG_EOCA);
            g_u8AdcBusy = 0U;
        }

        /* 1ms ʱ�� (Դ����: SysTick_Handler / TimingDelay_Decrement)��
           TODO: drv8301_protection() ���ٶȻ����Ĺ����ڴˡ� */
        if (g_u8Tick1ms != 0U) {
            static uint32_t s_u32Ms = 0UL;
            static uint32_t s_u32PcCmdCnt = 0UL;
            s_u32Ms++;

            /* PC host commands are parsed at 100 Hz (1 ms tick / 10). */
            if ((++s_u32PcCmdCnt % 10UL) == 0UL) {
                communication_handle();
            }

            /* ����������ʱ/����װ (�����¼����� EXTI �ж��ü���־) */
            Board_Key1msTask();

            if ((s_u32Ms % LED_TOGGLE_MS) == 0UL) {
                GPIO_TogglePins(USER_LED_PORT, USER_LED_PIN);   /* ������ */
            }
            g_u8Tick1ms = 0U;
        }

        /* TODO: drv8301_protection(); oled_display_handle(); */
    }
}

/**
 * @}
 */

/**
 * @}
 */

/*******************************************************************************
 * EOF (not truncated)
 ******************************************************************************/
