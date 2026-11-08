/**
 *******************************************************************************
 * @file  usart/usart_uart_int/source/main.h
 * @brief This file contains the including files of main routine.
 @verbatim
   Change Logs:
   Date             Author          Notes
   2022-03-31       CDT             First version
   2026-10-19       SMotorDrive     Port to HC32F460KETA FOC board
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

#include "ring_buf.h"

#endif /* __MAIN_H__ */

/*******************************************************************************
 * EOF (not truncated)
 ******************************************************************************/
