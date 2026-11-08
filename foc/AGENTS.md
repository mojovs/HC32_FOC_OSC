# AGENTS.md

HC32F460KETA（华大 XHSC）+ DRV8301 的 FOC 电机驱动固件。裸机、MDK/ARM Compiler 5、无 RTOS、
无测试、无 lint/CI。应用代码在 `source/`，其余为厂商 DDL 与本仓其他例程。
本工程已随 `HC32_FOC_OSC` 仓库发布（同级 `smdrive-host/` 为配套 PC 上位机；构建产物不入库，
见仓库根 `.gitignore`）。

## 本仓是什么（以及不是什么）

- 它是 `E:\pcb\SMotorDrive\配套代码\代码\code`（STM32F446 原版）的**移植进行中**工程。
  原版仓另有自己的 `AGENTS.md`，描述*设计意图*（控制环、FOC 数学、DRV8301 调参）——
  要理解“为什么”看它，**构建步骤不要看它**。
- 原版每个功能各先移植成一个独立示例，放在 `E:\pcb\SMotorDrive\code\example` 下：
  `gpio_output`、`usart_uart_int`、`usb_dev_cdc`、`drv8301_test`，以及 `foc`（本仓，合并目标）。
- **FOC 算法本体尚未移植。** `source/` 只有板级初始化 + USB CDC + PC 协议；
  `main.c::CompanionModulesInit()` 里 OLED、`drv8301_init()`、`foc_algorithm_initialize()` 仍是 TODO 桩。
  不要假设原版 `motor/*`（adc.c、low_task.c、speed_pid.c、ekf 封装）在本仓存在。
  唯一例外见下节：MATLAB 生成的 `foc_algorithm.c` 已被**引用**进 MDK 工程。
- `Readme.txt` 是 DDL 自带的“GPO样例”样例文本，与本工程无关——忽略。

## MATLAB 生成代码（`foc_algorithm`）

- `example/matlab/foc_algorithm_ert_rtw/` 是 **MATLAB/Simulink Coder 生成的代码，会被 MATLAB 反复重写**：
  **只引用、不要复制或手改**。稳定文件名 `foc_algorithm.c/.h`、`rtwtypes.h`；
  `ert_main.c` 含 `main()`，**不要编进固件**。
- MDK 引用方式（**Debug/Release 两个 target 都加**）：源文件
  `..\..\matlab\foc_algorithm_ert_rtw\foc_algorithm.c`（分组 `matlab`）；包含路径追加
  `../../matlab/foc_algorithm_ert_rtw`（取 `foc_algorithm.h`/`rtwtypes.h`）与 `../matlab_ert`。
- 生成头 `foc_algorithm.h` 会无条件 `#include "rtw_continuous.h"` / `"rtw_solver.h"`，而这两个 MathWorks
  头**不在生成目录里**。本仓 `foc/matlab_ert/` 放**空垫片**（原版 STM32 工程也是 0 字节）——本模型是纯代数
  子系统（clark/park/anti_park），用不到这两个头里的任何符号；这样构建不依赖 MATLAB 的安装路径/版本。
  若模型日后重新生成成含连续状态/求解器的代码，需把垫片换成真正的 MathWorks 头。
- 目前只是**引用进工程参与编译**：`main.c` 的 `foc_algorithm_initialize()` 仍是 TODO 桩，接线
  （`rtU.Vd/Vq/theta` → `rtY.Valpha/Vbeta`）等 FOC 整体移植时再做。
- `matlab/` 目前**未纳入 git**（untracked）。即仓库自身不含这份生成代码，新克隆后需先用 MATLAB 重新生成。
  若要“开箱即构建”，得把 `foc_algorithm_ert_rtw/` 入库（可忽略 `slprj/`、`*.mat`、`*.dmr` 等缓存）。

## 构建

- MDK 工程 `MDK/hc32f460_FOC.uvprojx`，器件 `HC32F460KETA`，Cortex-M4 FPU。
  Target：`hc32f460_FOC_Debug`、`hc32f460_FOC_Release`。
- 工具链固定在 **ARM Compiler 5**（`uAC6=0`；Debug `V5.06 update 7`，Release `V5.06 build 20`）。
  不要借无关改动顺手迁移到 AC6/GCC。
- 宏定义：`HC32F460,USE_DDL_DRIVER,XTAL_VALUE=16000000`（Debug 额外加 `__DEBUG`）。
- 产物为 `MDK/output/debug|release/hc32f460_FOC.axf` 与 `.hex`（`CreateHexFile=1`）；
  链接脚本 `MDK/config/linker/HC32F460xE.sct`（512 KB Flash / 192 KB SRAM）。
- **新增 `.c` 必须同时加入 `.uvprojx` 两个 target 的文件列表**——不会自动扫描。
  应用文件放进 `source` 分组。
- 驱动库是外部共享路径：`E:\pcb\SMotorDrive\code\HC32F460_DDL_Rev3.3.0`
  （包含路径 `../../../HC32F460_DDL_Rev3.3.0`）。其 `drivers/`、`midwares/`、`cmsis/` 一律当厂商代码。
- `UV4.exe` 不在 `PATH`；实际构建在 MDK IDE 里做。已入库的日志
  （`MDK/build*.log`）显示上次构建为 0 错误 / 0 警告。

不装 MDK 时单文件校验（只能抓语法/类型错误，不能链接）：

```powershell
$ddl="E:/pcb/SMotorDrive/code/HC32F460_DDL_Rev3.3.0"; $src="E:/pcb/SMotorDrive/code/example/foc/source"
$inc=@("$src","$ddl/drivers/cmsis/Device/HDSC/hc32f4xx/Include","$ddl/drivers/cmsis/Include",
  "$ddl/drivers/hc32_ll_driver/inc","$ddl/midwares/hc32/usb",
  "$ddl/midwares/hc32/usb/usb_device_lib/device_core",
  "$ddl/midwares/hc32/usb/usb_device_lib/device_class/single_cdc")
arm-none-eabi-gcc -c -fsyntax-only -Wall -mcpu=cortex-m4 -mthumb `
  -DHC32F460 -DUSE_DDL_DRIVER -DXTAL_VALUE=16000000 ($inc | % {"-I$_"}) "$src/main.c"
```

## 源码编码

- 应用源码 `foc/source/**` 为 **GBK/cp936**（Keil 的 `Encode in ANSI` / `Chinese GB2312` 即按此解码，
  默认设置即可，不要切 UTF-8）。编辑保持 GBK：按 UTF-8 读会显示乱码，按 UTF-8 回写会静默损坏
  中文注释；新注释也用 GBK。
- 已知：`board_config.c`、`main.c`、`cdc_data_process.c/.h` 的中文注释在移植时已被破坏
  （历史上被按 UTF-8 误存成 `�`，现已收敛为 `?`），git 里无完好版本；需要时按代码重写。
- 本仓工作路径为 ASCII（`code/example/...`），但移植来源与部分引用含中文路径（`配套代码`）；
  shell 命令里给路径加引号。

## 时钟 / 时序（与原版 STM32 不同）

- 200 MHz HCLK、100 MHz PCLK1、16 MHz XTAL；USB FS 需要独立的 **PLLX** 出 48 MHz
  （`board_config.c::SystemClockInit`，`board_config.h` 的 `USB_PLL_M/N/P`）。
- PWM 为 **20 kHz**（TMRA_1）——原版是 10 kHz。`FOC_PERIOD = 1/PWM_TIM_FREQ`；移植 FOC 算法时
  控制周期要与 `PWM_TIM_FREQ` 保持一致。
- ADC 采样走 **B2 方案（硬件触发 + DMA）**：`ADC1` 序列 A 由 `TMRA_1 OVF` 事件经 **AOS** 硬件触发
  （`board_config.c::AdcHardTriggerConfig`），结果由 **DMA1 CH0** 从 `DR2` 起搬 `ADC_DMA_BLOCK_SIZE`
  个 16-bit 到 `g_au16AdcValue`（`AdcDmaConfig`），块完成中断（`INT038`）里跑 `communication_task()`
  （`AdcDmaIrqCallback`，将来 FOC 也放这里）。HC32F460 的 ADC **有 DMA**（DDL 有 `adc_dma` 例程），
  原「ADC 无 DMA、只能轮询」的说法是错的。
- `board_config.c::Tmra1OvfIrqCallback()`（INT080 = TMRA_1 OVF，20 kHz）现在只派生 1 ms 节拍，
  不再 `ADC_Start`、不再调 `communication_task()`。
- `main.c` 主循环：`vcp_task()`、`communication_send_task()`、`UsbEventTask()`，
  以及 1 ms 分支（`Board_Key1msTask()` + PC 命令解析，每 10 次 ms = 100 Hz）。

## 外设 / 坑

- **寄存器写保护**：`main.c` 在启动时 `LL_PERIPH_WE(LL_PERIPH_SEL)`，在 `usb_dev_init()` 之后
  `LL_PERIPH_WP(LL_PERIPH_WP_SEL)`。任何在 WP 之后写 GPIO/FCG 等的初始化都会命中
  `DDL_AssertHandler()`（断点）。把初始化放到 WP 之前，或扩展 WP 掩码。
- 引脚/外设映射与“本板 vs 原版 STM32”对照表在 `source/board_config.h`。
- `printf` 走 **USART3 / CN1**（PB6 TX / PB7 RX），不是 USB。板→PC 的二进制数据用 `vcp_send()`。
- DRV8301 用**硬件 SPI2**（16-bit 帧，`board_config.c` 的 `Drv8301Write`/`Drv8301Read`）——
  原版是软件模拟 SPI。`drv8301_init()` 本身尚未移植。

## PC 上位机通信

- 原版 PC 链路是闭源 `.lib`；本仓**重写了同一套线协议**：引擎 `source/pc_communication.c`
  + 注册 `source/pc_communication_init.c`（API 名与原版 `communication.h` 一致）。
  协议规格：`E:\pcb\SMotorDrive\配套代码\代码\code\PC通信协议.md`。
- **`E:\pcb\SMotorDrive\配套代码\上位机安装包` 是我（用户）对 PC 上位机的逆向工程**（原厂上位机，本仓统一称「原工程」；
  Java Swing + RXTX）。排查“上位机为什么这么收发/这么解析”时以此为准，它是与固件等价的上位机侧实现：
  - `上位机逆向_通讯协议报告.md`：框架/协议。安装程序 `上位机安装程序.exe` 为 Inno Setup +
    install4j 启动器（XOR 0x88 内嵌 JAR），已还原并 CFR 反编译出 Java 源码；
    含固件侧 `pc_communication.a` 反汇编对照，**协议双向确认**。
  - `上位机_Qt开发计划书.md`：拟用 Qt 重写上位机的计划（去掉原版授权/联网/卡死缺陷，加离线回放）。
    本仓固件无需为其改动；“接收帧数/丢帧数”等 UI 概念与协议一致。
  - `_re\`：逆向产物（`bin\innoextract.exe`，`tools\*.py` 脚本，`jars\` 还原的 JAR，
    `src\app\` 反编译源码，`17_arm_handle*.txt`/`19_handle_annotated.txt` 固件侧反汇编）。
  - `原工程逆向归档_<日期>.zip`：上述结论的打包归档（文档＋脚本＋反编译源码＋证据清单＋SHA256SUMS）。
- **自研上位机客户端已落地：`E:\pcb\SMotorDrive\code\example\smdrive-host`**（Qt 5.15.2 / MSVC2019）。
  与固件线协议一致；`build.bat test` 跑协议 golden 用例与健壮性测试，`build.bat deploy` 出可分发目录，
  `packaging\make_installer.bat` 出安装包。固件无需为其改动。
- USB CDC 是 XHSC DDL 栈（在 DDL 树内）。板→PC 数据由 `vcp_send()` 追加进唯一的共享 IN 环
  `uart_rx_buffer[APP_RX_DATA_SIZE=2048]`；IN 传输由 SOF 驱动
  （`CDC_IN_FRAME_INTERVAL=5`）按 63 字节分块。PC→板经 `vcp_rxdata()` → `pc_communication_rx()`。
- `source/cdc_data_process.h` 的 `VCP_BRIDGE_TO_UART`（默认 **0**）控制 USB↔USART3 桥接与发往 PC 的
  欢迎文本。走二进制协议时保持 0。
- 要暴露一个信号，就在 `pc_communication_init.c` 里注册指针；头文件写明了回到原版 FOC 变量的
  通道映射。`PC_SAMPLE_BASE_HZ` 必须等于采样节拍率（20000），因为分频是 `PC_SAMPLE_BASE_HZ / host_freq`。

## 不要动

- DDL 树（`HC32F460_DDL_Rev3.3.0`）——厂商代码，被所有示例共享。
- `MDK/output/` 构建产物；`EventRecorderStub.scvd`、`JLinkSettings.ini`。
- 本仓没有测试/lint/格式化配置；验证方式 = MDK 构建 + 硬件行为（LED、USB CDC 枚举、PC 上位机帧）。
