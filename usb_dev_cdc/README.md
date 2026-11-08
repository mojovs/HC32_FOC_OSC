# usb_dev_cdc — USB 虚拟串口 (VCP) 移植

把 XHSC DDL 例程
`projects/ev_hc32f460_lqfp100_v2/applications/usb/usb_dev_cdc`
移植到 SMotorDrive 板（HC32F460KETA / LQFP64 / 16 MHz 晶振）。

它是一个**独立的 USB CDC bring-up 工程**，不是 FOC 固件。CDC 的“COM 口”与
USART3 双向桥接（和原例程行为一致），用来验证 USB 枚举、端点收发和 USART3 通路。

---

## 1. 硬件对应关系

| 功能 | 引脚 | 说明 |
|------|------|------|
| USB FS DM | PA11 | 内部 FS PHY，模拟模式 |
| USB FS DP | PA12 | 内部 DP 上拉由 `usb_devmodeinit()` 打开 |
| VBUS 检测 | 无 | **PA9 是 PWM_BH**，不能复用；已关闭 VBUS sensing |
| LED | PC9 | 低电平点亮，500 ms 心跳 |
| 调试串口 | PB6 = TX, PB7 = RX | USART3，`GPIO_FUNC_32/33`，Func_Grp2 |
| 调试座 | CN1 | 1=GND, 2=PB7(RX), 3=PB6(TX), 4=3.3V |

USB 口是 Type-C UFP（CC 上 5.1 k 下拉），只做设备，不能做 Host。
没有 5V_IN 时整板由 USB VBUS 经 Q12 供电（PC 口 500 mA 可能不够驱动栅极驱动/CAN）。

---

## 2. 时钟

```
XTAL 16 MHz ──> MPLL /2 *50 /2 = 200 MHz  (HCLK 200 MHz, PCLK1 100 MHz)
             └> PLLX /4 *84 /7 =  48 MHz  (USB FS 时钟, CLK_USBCLK_PLLXP)
```

- PLLX 与主 PLL 独立，不影响 FOC 用的 200 MHz / PCLK1 100 MHz。
- 48 MHz 必须精确，否则枚举不稳。`SystemClockInit()` 里对 XTAL/MPLL/PLLX
  都做了带超时的稳定等待，失败会停在 `for (;;)`（无 LED 提示，先查时钟）。
- `XTAL_VALUE=16000000` 同时出现在 `source/main.h` 和
  `MDK/usb_dev_cdc.uvprojx` 的 C/C++ Define 里，改时钟时两处都要改。

---

## 3. 相对原例程的修改

| 位置 | 修改 | 原因 |
|------|------|------|
| `usb_bsp.c` | 删除 `BSP_CLK_Init()` | EV BSP 按 8 MHz 晶振配 PLL，本板 16 MHz 会超频到 400 MHz |
| `usb_bsp.c` | 删除 `GPIO_SetFunc(PA9, GPIO_FUNC_10)`（VBUS） | PA9 = PWM_BH |
| `usb_bsp.c` | 删除 `BSP_LED_Init()` / `BSP_KEY_Init()` | EV 板 LED/按键在 PD3–PD6/PE6，LQFP64 没有 |
| `usb_bsp.c` | 写 `GVBUSCFG.VBUSOVEN\|VBUSVAL` | 没有 VBUS 引脚，强制内部“VBUS 有效” |
| `usb_bsp.c` | printf 改到 USART3 (PB6) | EV 板是 USART4/PE6 |
| `usb_bsp.c` | `DDL_PrintfInit()` 第二个参数传 baud/16 | DDL 把 `HCLK/参数` 当 TX 超时；200 MHz 下按波特率算只有约一个字节时间，会丢字符 |
| `usb_bsp.c` | USB 中断优先级 = `DDL_IRQ_PRIO_DEFAULT`(15) | 以后并入电机固件时，控制中断要能抢占 USB |
| `usb_app_conf.h` | 去掉 `VBUS_SENSING_ENABLED` | 同上，无 VBUS 引脚 |
| `cdc_data_process.c` | USART3 = PB6/PB7 (Func_Grp2) | 板级连线 |
| `cdc_data_process.c` | 默认 115200-8-N-1，`USART_CLK_DIV64` | PCLK1 = 100 MHz；CN1 无 RTS/CTS |
| `cdc_data_process.c` | `vcp_rxdata()` 只写环形缓冲，新增 `vcp_task()` | 原来在 USB 中断里阻塞等 UART 发完，64 字节约 5.5 ms |
| `cdc_data_process.c` | 波特率按 DIV64→DIV16→DIV4→DIV1 依次尝试 | 主机改波特率时（比如 921600）DIV64 分频不够 |
| `usb_dev_user.c` | `printf` 改为事件标志 `g_u8UsbEvt` | 这些回调在 USB 中断里，不能 printf |
| `usb_dev_desc.c` | 字符串改为 `SMotorDrive VCP` | 设备管理器里好认（VID/PID 仍是 0x2E88/0x4603） |
| `MDK/*.uvprojx` | PETB→KETA、路径改到 `../../../HC32F460_DDL_Rev3.3.0/`、删除 EV BSP 组、加 `XTAL_VALUE` | 板级工程 |

EWARM/ 和 GCC/ 目录仍是厂商原始文件（路径指向 `../../../../../../drivers`），
**未维护**，只用 MDK。

---

## 4. 编译

```powershell
& 'D:\Program Files\stm32\keil\UV4\UV4.exe' -r `
  'E:\pcb\SMotorDrive\code\example\usb_dev_cdc\MDK\usb_dev_cdc.uvprojx' `
  -j0 -o 'E:\pcb\SMotorDrive\code\example\usb_dev_cdc\MDK\build.log'
```

- Debug: `MDK/output/debug/usb_dev_cdc.hex`（Code ≈ 25 KB, ZI ≈ 12.6 KB）
- Release: `MDK/output/release/usb_dev_cdc.hex`（Code ≈ 17.5 KB）
- UV4 退出码 0 = 无错无警告。Keil IDE 打开着这个工程时命令行编译会被锁。

---

## 5. 测试步骤

1. 只插 USB（或 5V_IN + USB）。PC9 应看到 500 ms 心跳 —— 证明 16 MHz/200 MHz 时钟正常。
2. 设备管理器出现 `SMotorDrive VCP`（USB 串行设备，无需装驱动）。
3. 打开该 COM 口（115200-8-N-1）。打开串口后应看到：
   ```
   [host opened the port]
   Type anything: the board echoes it back (no CN1 jumper needed).
   ```
   如果之前那一句 `SMotorDrive VCP ready.` 没被上位机清掉，还会多看到一行——这是正常的：
   问候语在枚举完成时就发了，而很多串口助手打开端口时会清空驱动 RX 缓冲区，
   所以板子在**打开端口时（DTR）**和**第一次收到 PC 数据时**各补发一次。
   两者都不经过 CN1，走的是 USB 端点。

   同时 CN1（物理串口，115200-8-N-1）上会打印启动横幅：
   ```
   ========================================
    SMotorDrive USB CDC test
    HC32F460KETA, 16 MHz XTAL, 200 MHz
    USB: PA11(DM)/PA12(DP), CDC <-> USART3
    CN1: 2=PB7(RX), 3=PB6(TX), 115200-8-N-1
   ========================================
   ```
4. **回显测试（不需要短接任何东西）**：在串口终端里直接打字，板子会把收到的每个字节
   原样从 USB 发回来，所以屏幕上能看到自己输入的内容 —— 这就同时证明了
   PC→板（OUT）和 板→PC（IN）两个方向都通。
   这个本地回显由 `cdc_data_process.c` 顶部的 `VCP_LOCAL_ECHO` 控制；
   把它改成 `0` 就恢复成纯桥接（PC → USART3，只往 CN1 送，不回显）。
   开着回显时再用 USB-TTL 适配器接 CN1 会看到两遍字符（本地回显 + 真正的串口回环），
   属于正常现象。
5. **真正的串口回环**：CN1 的 2、3 脚短接，或在 CN1 接 USB-TTL 适配器
   （适配器 TX→CN1-2，RX→CN1-3，GND→CN1-1），用两个终端互发。
   **CN1-4 是 3.3V，不要接 GND。**（CN1 是 2.54 排针，不好短接就用适配器。）
6. 在终端里把波特率改成别的值，桥接侧会跟着改（USART3 重新配置）。

---

## 6. 并入 drv8301_test / FOC 固件时

- 只用 `vcp_task()` 在主循环里调用一次；`vcp_init()` 由 CDC 类在枚举时自动调用。
- USB 中断优先级 15、USART3 中断优先级 15；ADC/PWM/控制中断要用更小（更高）
  的优先级，别让 USB 拖慢 20 kHz 环。
- `printf`（DDL_Printf）和 CDC→UART 的数据都走 USART3 TX：
  合并后若 USART3 已被 FOC 控制台占用，要么把 CDC 桥去掉只留 VCP，
  要么把 `PRINT_UNIT` 换成别的串口。
- `APP_RX_DATA_SIZE` = 2 KB（UART→USB 缓冲），`VCP_TX_RING_SIZE` = 1 KB（USB→UART）。
  当前 UART→USB 方向没有满队列保护（沿用厂商 CDC 类的单指针环形缓冲），
  115200 下不会溢出；跑到 921600 以上要自己加水位判断。
- 往 PC 发数据用 `vcp_send()`（直接写 CDC IN 缓冲，不经 USART3）；
  `printf` 只走 USART3。
- `usb_app_conf.h` 里 FIFO 合计 256 B（上限 320 B），要加复合设备（HID+CDC）时注意。

---

## 7. 如果枚举失败，按这个顺序查

1. `CLK_GetStableStatus(CLK_STB_FLAG_PLLX)` 是否通过 —— 代码会停在 `for(;;)`。
   量 PA11/PA12 有没有 48 MHz 相关活动，或临时在 `SystemClockInit()` 后点亮 LED。
2. `GVBUSCFG`（`usb_bsp.c` 末尾）：没有 VBUS 引脚时必须 `VBUSOVEN=1, VBUSVAL=1`。
3. PA11/PA12 必须配置成 `PIN_ATTR_ANALOG`，不能被别的外设占用。
4. 16 MHz 晶振（X3）真的起振；原理图 PDF 上标的 8 MHz 是旧版，以代码/硬件文档为准。
5. USB 线是否只有供电没有数据（部分线只有 VBUS/GND）。

## 8. 枚举成功但没有输出

按顺序区分「IN 方向坏了」还是「上位机把数据丢了」：

1. **先做回环**：CN1 的 2、3 脚短接，在终端里打字。
   - 能原样回显 → CDC IN/OUT 都正常，之前的"没输出"是上位机在打开端口时
     清空了驱动缓冲区（`PurgeComm(PURGE_RXCLEAR)`），旧固件只在枚举时发一次就没了。
   - 不回显 → IN 端点/`APP_Rx_ptr_out` 状态有问题，查 `USB_Tx_State`。
2. 当前固件在三个时机各补一次提示：枚举完成（`SMotorDrive VCP ready.`）、
   打开端口/DTR（`[host opened the port]`）、收到 PC 第一包数据。看到重复属正常。
3. 串口助手如果默认不发 DTR，把"打开串口时置 DTR/RTS"勾上，或随便发一个字符。
