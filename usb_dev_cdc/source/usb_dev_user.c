/**
 *******************************************************************************
 * @file  usb_dev_cdc/source/usb_dev_user.c
 * @brief User callback function for USB example
 *
 * The callbacks in this file run inside the USB interrupt (usb_isr_handler),
 * therefore they only publish a one-byte event code. The main loop prints it
 * through UsbEventTask() in main.c. Calling printf() here would block the USB
 * interrupt for milliseconds.
 @verbatim
   Change Logs:
   Date             Author          Notes
   2022-03-31       CDT             First version
   2026-10-20       SMotorDrive     Replace printf-in-ISR with event flags
 @endverbatim
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
#include "usb_dev_user.h"

/**
 * @addtogroup SMotorDrive_USB_Dev_Cdc
 * @{
 */

/*******************************************************************************
 * Local function prototypes ('static')
 ******************************************************************************/
void usb_dev_user_init(void);
void usb_dev_user_rst(void);
void usb_dev_user_devcfg(void);
void usb_dev_user_devsusp(void);
void usb_dev_user_devresume(void);
void usb_dev_user_conn(void);
void usb_dev_user_disconn(void);

/*******************************************************************************
 * Global variable definitions (declared in header file with 'extern')
 ******************************************************************************/
usb_dev_user_func user_cb = {
    &usb_dev_user_init,
    &usb_dev_user_rst,
    &usb_dev_user_devcfg,
    &usb_dev_user_devsusp,
    &usb_dev_user_devresume,
    &usb_dev_user_conn,
    &usb_dev_user_disconn,
};

/* Latest stack event, consumed by the main loop. A single byte is enough:
   the consumer only reports the most recent state, and a lost intermediate
   event during enumeration is harmless. */
volatile uint8_t g_u8UsbEvt = USB_EVT_NONE;

/*******************************************************************************
 * Function implementation - global ('extern') and local ('static')
 ******************************************************************************/
/**
 * @brief  usb_dev_user_init
 * @param  None
 * @retval None
 */
void usb_dev_user_init(void)
{
    /* Add initial code here */
}

/**
 * @brief  usb_dev_user_rst
 * @param  None
 * @retval None
 */
void usb_dev_user_rst(void)
{
    g_u8UsbEvt = USB_EVT_RESET;
}

/**
 * @brief  usb_dev_user_devcfg
 * @param  None
 * @retval None
 */
void usb_dev_user_devcfg(void)
{
    g_u8UsbEvt = USB_EVT_CONFIG;
}

/**
 * @brief  usb_dev_user_conn
 * @param  None
 * @retval None
 */
void usb_dev_user_conn(void)
{
    /* Only reachable with VBUS sensing enabled, which this board does not use. */
}

/**
 * @brief  usb_dev_user_disconn
 * @param  None
 * @retval None
 */
void usb_dev_user_disconn(void)
{
    g_u8UsbEvt = USB_EVT_DISCONN;
}

/**
 * @brief  usb_dev_user_devsusp
 * @param  None
 * @retval None
 */
void usb_dev_user_devsusp(void)
{
    g_u8UsbEvt = USB_EVT_SUSPEND;
}

/**
 * @brief  usb_dev_user_devresume
 * @param  None
 * @retval None
 */
void usb_dev_user_devresume(void)
{
    g_u8UsbEvt = USB_EVT_RESUME;
}

/**
 * @}
 */

/******************************************************************************
 * EOF (not truncated)
 *****************************************************************************/
