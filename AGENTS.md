# AGENTS.md — HC32_FOC_OSC 工作区记忆

## 这是什么

HC32F460KETA（华大 XHSC）+ DRV8301 的 **FOC 电机驱动固件**，外加配套的 **PC 上位机**与若干外设测试例程。
远端：`git@github.com:mojovs/HC32_FOC_OSC.git`（分支 `main`）。
上手入口：先读 `README.md`（仓库总览），固件的设计意图读 `foc/AGENTS.md`，上位机读 `smdrive-host/README.md`。

## 目录结构

| 路径 | 说明 |
|---|---|
| `foc/` | FOC 固件主工程：MDK / EWARM / GCC 三套工程；**应用代码只在 `foc/source/`**，其余是厂商 DDL 引用与工程配置 |
| `smdrive-host/` | PC 上位机（Qt 5.15.2 / MSVC2019，C++20）：串口协议、实时波形、参数下发、留档与离线回放 |
| `drv8301_test/` `gpio_output/` `usart_uart_int/` `usb_dev_cdc/` | 外设例程（DRV8301 寄存器、GPIO、串口中断、USB CDC），调试期用于验证硬件通路 |
| `.gitignore` | 产物与留档数据的统一排除规则（MDK output / EWARM Obj/List / GCC build / 上位机 build-dist-installer） |

## 构建与运行

**固件**（打开对应工程文件即可）

- MDK：`foc/MDK/hc32f460_FOC.uvprojx`（ARM Compiler 5）
- IAR：`foc/EWARM/gpio_output.eww`（注意：文件名沿用了 SDK 例程名，实际是本 FOC 工程）
- GCC：`foc/GCC/`（Eclipse 工程，含 `.cproject`）

固件依赖厂商 DDL，**不在本仓库**：本机位于 `E:\pcb\SMotorDrive\code\HC32F460_DDL_Rev3.3.0`（IDE 支持包 `HC32F460_IDE_Rev1.0.12` 同目录）。换机器需自备 DDL 并改工程内的相对/绝对引用。

**上位机**

```bat
cd smdrive-host
build.bat                     :: 配置 + 编译            -> build\smdrive-host.exe
build.bat test                :: 编译 + 协议单元测试
build.bat deploy              :: windeployqt 出可分发目录 -> dist\
packaging\make_installer.bat  :: 出安装包               -> installer\*.exe
```

工具链写死在 `build.bat` 里（Qt kit `G:\Qt\5.15.2\msvc2019_64`、VS2019 的 vcvarsall/CMake/Ninja 绝对路径），换机器改这几个变量即可。
开发态运行需把 `G:\Qt\5.15.2\msvc2019_64\bin` 加入 PATH，或直接用 `dist\`（自足）。

## 测试与验证

- 上位机单元测试：`smdrive-host\build.bat test`（ctest，1 个 target `protocol`，17 个用例：协议 golden 帧逐字节比对、0 值参数帧、逐字节切分/分包半帧/噪声/坏帧尾/payload 内伪造帧头、认证帧、未知命令、环形缓冲回绕、整型波形符号、串口助手发送编码与接收渲染）。
- **无硬件也能验证全链路**：`smdrive-host\tools\frame_injector.py` 按协议造 `.bin` 流，然后
  `smdrive-host.exe --replay <file.bin> --fps 250 --rec-csv out.csv --auto-close 5000`；退出时打印一行统计（`dataFrames/droppedFrames/bytesIn/rows`）便于脚本断言。
- 界面自查：`--shot <png> --shot-delay 3000` 抓图（offscreen 平台没有系统字体，截图文字会是空白框，要看真实界面用默认 windows 平台）；`--ui-mode serial` 可直接截串口助手界面。
- 固件侧无自动化测试：只能靠编译 + 上板跑，改动后至少确认三套工程都能编译通过。

## 代码约定

- **foc 应用源码统一 GBK/cp936**：`foc/source/` 的 `.c/.h` 全部为 GBK；Keil 用默认的 GB2312/ANSI 即正常，不要切 UTF-8。编辑保持 GBK，新注释也用 GBK（写成 UTF-8 会显示乱码）。注意 `board_config.c`、`main.c`、`cdc_data_process.c/.h` 的中文注释在移植时已被破坏（现为 `?`），git 里无完好版本，需要时按代码重写。
- 固件应用代码只放 `foc/source/`，不要改厂商 DDL；三套工具的工程文件改动要同步（MDK/EWARM/GCC）。
- 上位机：协议常量与帧结构集中在 `src/protocol/frame.h`，通道与参数元数据只维护 `src/app/channel_table.h`（改固件索引要同步这里）。每通道的显示颜色/线型/线宽是**运行时**状态，存在 `WaveformView`（`m_colors/m_styles/m_widths`，默认色取自 `channel_table.h`），由右栏「波形通道」行尾的样式按钮（`main_window.cpp::rebuildChannelStyleMenu`）修改，只影响本地绘图。
- 上位机界面：**顶部＝菜单栏＋运行工具栏**（`main_window.cpp::buildMenus()`：菜单栏＝文件（回放/记录/参数方案/退出）＋帮助（关于）；运行工具栏＝认证/显示波形(F6)/启停/加减速，其中「显示波形」即协议 03「开始上传」），右栏只留高频调控项（连接/显示/波形通道/参数·数据表）。给功能加按钮前先判断是否该进菜单——不要双入口。
- 上位机右栏顶部有 **「控制台 / 串口助手」模式开关**（`main_window.cpp::onModeChanged`）：切到串口助手时左主区换成原始串口终端（`SerialAssistant`，类似 XCOM），右栏换成串口参数；收发编解码纯逻辑在 `src/app/serial_text_codec.*`（可单测），控件不直接碰串口，原始字节经 `ITransport::bytesReceived` 中转。终端的工具条有状态圆点（绿=已连接/红=已断开）、`打开/断开串口`（发 `requestToggleConnection`，主窗口复用 `onTogglePort`）与 `清除接收`/`清空`。串口助手模式下打开串口**不自动补发认证帧**。
- 协议两端对齐：固件的 `source/pc_communication*.c` 与上位机的 `protocol/` 是同一套线协议，改一侧必须改另一侧。
- 命名约定：文档与代码里用「原工程」指代原厂上位机及其厂商名，不出现原始品牌字样。
- 提交信息里**不要出现 `<` `>`**（经 cmd 传参会变成重定向）；需要多行信息时写进文件用 `git commit -F <file>`。

## 已知坑

- `foc/EWARM/gpio_output.ewp|.eww`、`GCC/gpio_output_*.launch` 的名字是抄 SDK 例程留下的，内容属于 FOC 工程——改名要同步工程内部引用，别只看名字。
- `dist/`、`installer/` 是产物：**日常改动验证只用 `build.bat test`，不要顺手 deploy**；仅在用户明确要分发时才 `build.bat deploy`（必要时再 `make_installer.bat`）。分发前必须重新 deploy，否则给的是旧版本；拿新参数跑旧 exe 会因未知选项直接退出。
- 上位机的整型量（波形与标量）线上是 u32 位模式、语义是 s32：**必须按有符号解释**，否则负样本显示成 4.29e9 级别巨值并把自动量程撑爆。
- 参数下发帧恒定 4 字节大端：历史实现曾在 `0` 值上因十六进制不补前导零而发出错位帧，改这块务必跑 `paramZeroStillFourBytes` 用例。
- 上位机打开串口时显式把 DTR/RTS 拉低；某些 STM32/CDC 实现对电平翻转敏感，**这条尚未在真机上验证**。
- **上位机 `build/` 目录跨任务反复复用可能残留旧目标文件**：改了头文件（如给类加成员）后，个别 `.obj` 未随之重编，会出现「结构体大小/成员布局不一致」的 ABI 错配，表现为 **Release 退出时堆损坏（0xC0000374 崩溃），而 Debug/RelWithDebInfo 或新建构建目录却正常**。遇到这类诡异崩溃先 `rm -rf smdrive-host/build` 全量重编再判断，别急着改代码。
- 构建产物不入库（见 `.gitignore`）；`*.hex` 也被排除，要发布固件二进制需单独放目录再加反排除规则。

## 相关文档

- `README.md`：仓库总览与构建入口
- `smdrive-host/README.md`：上位机命令行参数、界面布局（`docs/ui.png`）、已实测数据
- `foc/AGENTS.md`：固件设计意图、时钟/时序与原版差异、移植来源说明
- 协议逆向依据（原厂上位机分析、协议对照表、归档 zip）为本地私有资料，未随本仓库发布
