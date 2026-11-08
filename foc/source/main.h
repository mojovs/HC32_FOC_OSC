/**
 *******************************************************************************
 * @file  foc/source/main.h
 * @brief This file contains the including files of main routine.
 *
 * foc 工程: HC32F460KETA + DRV8301 FOC 板级固件。
 * USB CDC 栈需要的设备/描述符/类头文件在这里统一引入 (从 usb_dev_cdc
 * 工程移植)。
 @verbatim
   Change Logs:
   Date             Author          Notes
   2022-03-31       CDT             First version
   2026-10-19       SMotorDrive     Port to HC32F460KETA FOC board
   2026-10-26       SMotorDrive     + USB CDC (usb_dev_cdc 合并)
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

/* External high speed crystal frequency on this board: 16MHz.
   Confirmed by the board owner. Also defined in the project preprocessor
   symbols: XTAL_VALUE=16000000. */
#ifndef XTAL_VALUE
#define XTAL_VALUE                  (16000000UL)
#endif

#include "hc32_ll.h"

/* USB 设备栈: 用户回调 / 设备描述符 / CDC 类 (usb_dev_cdc 工程) */
#include "usb_dev_user.h"
#include "usb_dev_desc.h"
#include "usb_dev_cdc_class.h"

#endif /* __MAIN_H__ */

/*******************************************************************************
 * EOF (not truncated)
 ******************************************************************************/
