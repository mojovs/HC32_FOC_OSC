/*
 * rtw_continuous.h - empty shim for the MATLAB/Simulink Coder build.
 *
 * The Simulink-generated header (matlab/foc_algorithm_ert_rtw/foc_algorithm.h)
 * unconditionally pulls in
 *     #include "rtw_continuous.h"
 *     #include "rtw_solver.h"
 * inside its COMMON_INCLUDES block.  For a purely algebraic model
 * (Clarke / Park / inverse-Park, no continuous states and no solver support)
 * none of the symbols declared by those MathWorks headers are actually used,
 * so empty shims are enough to satisfy the includes.
 *
 * Keeping the shims here -- instead of pointing the include path at the local
 * MATLAB installation's simulink\include -- makes the firmware build
 * self-contained and independent of the MATLAB version / install path.
 * This mirrors the original STM32 project, which shipped the same two headers
 * as 0-byte files next to its generated foc_algorithm.c.
 *
 * NOTE: if the MATLAB model is later regenerated with solver-dependent
 * features (continuous states, variable step, zero crossings, ...) this stub
 * must be replaced by the real MathWorks header.
 */
#ifndef RTW_CONTINUOUS_H__
#define RTW_CONTINUOUS_H__

#endif /* RTW_CONTINUOUS_H__ */
