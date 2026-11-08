/**
 *******************************************************************************
 * @file  usb_dev_cdc/source/main.h
 * @brief Head file of main routine.
 *
 * Ported from the XHSC DDL example
 *   projects/ev_hc32f460_lqfp100_v2/applications/usb/usb_dev_cdc
 * to the SMotorDrive board (HC32F460KETA, LQFP64, 16 MHz XTAL).
 @verbatim
   Change Logs:
   Date             Author          Notes
   2022-03-31       CDT             First version
   2026-10-20       SMotorDrive     Port to HC32F460KETA + 16 MHz XTAL,
                                    USB on PA11/PA12, console on USART3
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
#ifndef __MAIN_H__
#define __MAIN_H__

#include "hc32_ll.h"
#include "usb_dev_user.h"
#include "usb_dev_desc.h"
#include "usb_dev_cdc_class.h"

/*
 * X3 on this board is 16 MHz (the EV board reference uses 8 MHz).
 * Keep this in sync with the XTAL_VALUE define in the Keil project
 * (MDK/usb_dev_cdc.uvprojx -> C/C++ -> Define).
 */
#ifndef XTAL_VALUE
#define XTAL_VALUE                      (16000000UL)
#endif

#endif /* __MAIN_H__ */

/*******************************************************************************
 * EOF (not truncated)
 ******************************************************************************/
