/**
 *******************************************************************************
 * @file  usb/usb_dev_cdc/source/usb_dev_user.h
 * @brief Head file for usb_dev_user.c
 @verbatim
   Change Logs:
   Date             Author          Notes
   2022-03-31       CDT             First version
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
#ifndef __USB_DEV_USER_H__
#define __USB_DEV_USER_H__

/* C binding of definitions if building with C++ compiler */
#ifdef __cplusplus
extern "C"
{
#endif

/*******************************************************************************
 * Include files
 ******************************************************************************/
#include "usb_dev_core.h"

/**
 * @addtogroup HC32F460_DDL_Applications
 * @{
 */

/**
 * @addtogroup USB_Dev_Cdc
 * @{
 */

/*******************************************************************************
 * Global type definitions ('typedef')
 ******************************************************************************/
extern  usb_dev_user_func user_cb;

/*******************************************************************************
 * Global pre-processor symbols/macros ('#define')
 ******************************************************************************/
/*
 * The user callbacks below are invoked from the USB interrupt. They must not
 * call printf(): instead they publish the latest event here and the main loop
 * prints it (see UsbEventTask() in main.c).
 */
#define USB_EVT_NONE                    (0U)
#define USB_EVT_RESET                   (1U)
#define USB_EVT_CONFIG                  (2U)
#define USB_EVT_SUSPEND                 (3U)
#define USB_EVT_RESUME                  (4U)
#define USB_EVT_DISCONN                 (5U)

/*******************************************************************************
 * Global variable declarations ('extern', definition in C source)
 ******************************************************************************/
extern volatile uint8_t g_u8UsbEvt;

/*******************************************************************************
  Global function prototypes (definition in C source)
 ******************************************************************************/

/**
 * @}
 */

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* __USB_DEV_USER_H__ */

/*******************************************************************************
 * EOF (not truncated)
 ******************************************************************************/
