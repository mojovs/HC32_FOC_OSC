# HC32_FOC_OSC

HC32F460（华大 XHSC / XIAOHUA）+ DRV8301 的 FOC 电机驱动固件，以及配套的 PC 上位机与外设测试例程。

| 路径 | 说明 |
|---|---|
| `foc/` | FOC 固件主工程：MDK（ARM Compiler 5）、EWARM、GCC 三套工程，应用代码在 `source/` |
| `smdrive-host/` | PC 上位机（Qt 5.15.2 / MSVC2019）：串口实时波形、参数在线下发、CSV/原始流留档与离线回放、拔插重连 |
| `drv8301_test/` | DRV8301 驱动/寄存器测试例程 |
| `gpio_output/` | GPIO 输出例程 |
| `usart_uart_int/` | 串口中断收发例程 |
| `usb_dev_cdc/` | USB CDC（虚拟串口）例程，上位机通信链路的基础 |

## 构建

**固件**：用对应工具链打开工程文件即可

- MDK：`foc/MDK/*.uvprojx`
- IAR：`foc/EWARM/*.eww`
- GCC：`foc/GCC/`

固件依赖厂商 DDL（`HC32F460_DDL_Rev3.3.0`，**未随本仓库发布**），请自备对应版本并按工程内的相对/绝对路径配置调整。

**上位机**：

```bat
cd smdrive-host
build.bat                     :: 配置 + 编译
build.bat test                :: 编译 + 协议单元测试
build.bat deploy              :: windeployqt 出可分发目录 dist\
packaging\make_installer.bat  :: 出安装包 installer\*.exe
```

无硬件调试时可用 `--replay <file.bin>` 回放留档的原始字节流验证全链路。详见 `smdrive-host/README.md`。
