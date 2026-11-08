SMotorDrive / drv8301_test
=========================

HC32F460KETA + DRV8301 hardware bring-up test (NOT a FOC controller).

Purpose
-------
 1. DRV8301 SPI register write / read-back / fault status
 2. TMRA_1 3-PWM output (DRV8301 internal dead time)
 3. Open-loop V/f spin (align 300 ms, then ramp)
 4. Two shunt currents, Vbus, NTC, hall inputs
 5. USART3 115200 command console

Build
-----
  Keil MDK 5.36, target drv8301_test_Debug / drv8301_test_Release.
  Output: MDK/output/debug/drv8301_test.hex

  UV4.exe -r drv8301_test.uvprojx -j0 -o build.log

Console (115200-8-N-1 on CN1: 1=GND 2=RX 3=TX, 4=3.3V not used)
----------------------------------------------------------------
  s      start / stop
  + -    electrical frequency +/- 1 Hz
  a z    modulation index +/- 0.02
  d      DRV8301 registers / faults
  r      wake, clear faults, reconfigure, recalibrate (stop first)
  m      monitor line on/off
  h      one status line
  ?      help

Keys: KEY1 start/stop, KEY2 f+, KEY3 f-.
LED PC9: 1 Hz blink = idle, on = running, 10 Hz = fault.

Safety
------
  EN_GATE low = all gates off (idle, stopped, any fault).
  A full EN_GATE low resets the DRV8301, so every start re-writes and
  verifies CR1/CR2 before the gate driver is enabled.
  3-PWM mode only: INH = PA8/PA9/PA10, INL (PB13/14/15) held low.

See README.md for the complete pin map, expected output and step-by-step
bring-up procedure.
