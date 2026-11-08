# DRV8301 硬件验证工程（HC32F460KETA）

这是一个 **硬件点检 / 上电验证** 工程，不是 FOC 控制器。目的是在给电机施加功率之前，逐级确认：

1. DRV8301 的 SPI 能否读写、寄存器配置是否生效、故障状态是否干净；
2. 三相 PWM 能否正常输出（3-PWM 模式，死区由 DRV8301 内部产生）；
3. 低侧采样电阻的电流、母线电压、NTC 温度、霍尔输入是否正常；
4. 最后用 **开环 V/f**（先对齐、再缓慢升频）让电机低速转起来，验证功率桥和电机。

工程位置：`code/example/drv8301_test`，Keil 目标 `drv8301_test_Debug` / `drv8301_test_Release`。

---

## 1. 硬件映射

引脚全部取自 `硬件/Netlist_Schematic1_2026-09-11.net`，不是 STM32 参考工程的 `board_config.h`。

| 功能 | MCU 引脚 | 说明 |
| --- | --- | --- |
| PWM_AH / BH / CH | PA8 / PA9 / PA10 | `TIMA_1_PWM1/2/3`，`GPIO_FUNC_4` |
| PWM_AL / BL / CL | PB13 / PB14 / PB15 | 固定输出低；3-PWM 模式下 INL 为 Don't Care |
| nSCS | PA15 | GPIO 输出（复位是 JTDI，必须先释放 JTAG） |
| SCLK | PC10 | 硬件 SPI2_SCK，`GPIO_FUNC_47` |
| SDO (MISO) | PC11 | 硬件 SPI2_MISO，`GPIO_FUNC_45` |
| SDI (MOSI) | PC12 | 硬件 SPI2_MOSI，`GPIO_FUNC_44` |
| EN_GATE | PB9 | 高有效；低电平关闭全部栅极 |
| DC_CAL | PB8 | 高电平短接电流放大器输入（零电流校准） |
| nFAULT | PB2 | 输入，上拉，低有效 |
| nOCTW | PC6 | 输入，上拉，低有效（过温/过流） |
| PWRGD | PB5 | 输入（Buck 电源好），仅打印 |
| VBUS | PC0 / ADC1_CH10 | 分压 95.3 kΩ / 4.99 kΩ |
| IA (SO1) | PC1 / ADC1_CH11 | 低侧 0.02 Ω 采样，增益 10 |
| IB (SO2) | PC2 / ADC1_CH12 | 低侧 0.02 Ω 采样，增益 10 |
| I_Total | PA2 / ADC1_IN2 | U9 TP181A1（G=50）+ R57 5 mΩ，平均母线电流 |
| NTC | PC3 / ADC1_CH13 | 10 kΩ NTC + 4.7 kΩ 上拉 |
| HALL_A/B/C | PA0 / PA1 / PA3 | 上拉输入 |
| KEY1 / KEY2 / KEY3 | PD2 / PB3 / PB4 | 按下为低 |
| LED4 | PC9 | 低电平点亮 |
| 调试串口 | PB6 (TX) / PB7 (RX) | USART3，115200-8-N-1 |

CN1 调试口：**1=GND，2=PB7(RX)，3=PB6(TX)，4=3.3 V**。
USB 转串口接法：适配器 TX → CN1-2，适配器 RX → CN1-3，GND → CN1-1，**CN1-4 悬空**。

SPI 配置：**硬件 SPI2**，`SPI_MD_1`（CPOL=0、CPHA=1）、16 bit、MSB first、PCLK1/64 = 1.5625 MHz；
nSCS 仍是 PA15 GPIO，由软件在每帧前后拉低/拉高。PC10/PC11/PC12 属于 `Func_Grp1`，
按数据手册表 2-2 用 `GPIO_FUNC_47/45/44` 映射到 SPI2_SCK/MISO/MOSI。

---

## 2. 编译与烧录

```powershell
& 'D:\Program Files\stm32\keil\UV4\UV4.exe' -r `
  'E:\pcb\SMotorDrive\code\example\drv8301_test\MDK\drv8301_test.uvprojx' `
  -j0 -o 'E:\pcb\SMotorDrive\code\example\drv8301_test\MDK\build.log'
```

输出：`MDK/output/debug/drv8301_test.hex`（Release 在 `output/release/`）。
构建结果为 0 error / 0 warning 才继续。

时钟：16 MHz 晶振 → MPLL 200 MHz，PCLK1 = 100 MHz，PWM 20 kHz（周期 2500）。

---

## 3. 上电前检查

1. **先不接电机**，只给板子控制电源（USB 或 5 V）；确认 3.3 V 正常。
2. 直流电源设 **12 V / 限流 1 A**，接到 PVDD（电机电源）。
3. 串口 115200 打开，确认复位后有横幅输出。
4. 上电瞬间功率桥不应发热，电源不应限流。

> 注意：本板实际晶振是 **16 MHz**（代码、工程宏、文档均已按 16 MHz）。PDF 原理图上标 8 MHz 是旧标注。

---

## 4. 首次上电的预期输出

```
========================================
 HC32F460KETA + DRV8301 bring-up test
 20 kHz 3-PWM, open-loop V/f (no FOC)
========================================
DRV8301 CR1=0399 (expect 0399) CR2=0000 (expect 0000)
TP181: offset 2048 counts
DRV8301: SPI OK
DRV8301 status:  SR1=0000 SR2=00xx
Current amp offset: IA=2048 IB=2048 counts (ok)
DRV8301: configured (now in reset)
Vbus=12.0 V  hall=5  nFAULT=1
Default: f=3.0 Hz, mod=0.10. Type '?' for help.
[IDLE] f=0.0Hz m=0.00 V=12.0V I=--A It=0.00A T=27.5C hall=5 drv=off
```

判断标准：

| 现象 | 结论 |
| --- | --- |
| `CR1=0399` | SPI 写入/回读正常，3-PWM、0.7 A 栅极电流、OC 锁存、0.317 V |
| `SR1=0000` | 无故障 |
| `SR1` 有 `PVDD_UV`/`GVDD_UV` | 电机电源未上、电压过低或电荷泵异常 |
| `SPI FAILED` | 检查 PVDD、SPI 四根线、EN_GATE、3.3 V |
| `offset` 在 1900~2200 | 电流放大器与 REF=1.65 V 正常 |
| `offset` 为 0 或 4095 | 放大器未供电（EN_GATE/PVDD）或采样通路断开 |
| `Vbus` 与万用表一致 | 分压比正确（误差 < 0.3 V） |
| `T` 接近室温 | NTC 通路正确 |
| `hall` 在 1~6 之间 | 霍尔有输出（手转电机应依次变化） |

`drv=off` 是正常的：空闲时 EN_GATE 为低，DRV8301 处于复位/低功耗状态。

---

## 5. 串口命令

| 命令 | 功能 |
| --- | --- |
| `s` | 启动 / 停止（启动前会重新唤醒并配置 DRV8301） |
| `+` / `-` | 电频率 +1 Hz / -1 Hz（0.5 ~ 60 Hz） |
| `a` / `z` | 调制比 +0.02 / -0.02（0.01 ~ 0.60） |
| `d` | 打印 SR1/SR2/CR1/CR2 与 nFAULT/nOCTW/PWRGD |
| `e` | 让 EN_GATE 持续为高，便于用万用表量 U1 供电脚；再按一次释放 |
| `t` | 把 SPI2 降到 390 kHz（PCLK1/256）重读 4 个寄存器，再恢复 1.5625 MHz |
| `r` | 唤醒、清故障、重新配置并重新校准（需先停止） |
| `m` | 1 Hz 监视行开关（觉得刷屏就关掉） |
| `h` | 打印一次状态 + 寄存器 |
| `?` | 帮助 |

按键：KEY1 = 启动/停止，KEY2 = 频率 +1 Hz，KEY3 = 频率 -1 Hz。
按键事件会以 `>> KEYx: ...` 单独一行打印（前面空一行），不会被 1 Hz 监视行淹没。
LED：空闲 1 Hz 慢闪，运行常亮，故障 10 Hz 快闪。

---

## 6. 分阶段验证步骤

### 阶段 A：只验证 DRV8301（不接电机，12 V / 1 A）

1. 上电，确认 `SPI OK`、`SR1` 无故障、`Vbus` 正确；日志中的 `TP181: offset` 应接近 2048（1500~2600），超出说明 U9 供电/REF 异常。
2. 发送 `d`，应看到 `CR1=0399 CR2=0000 nFAULT=1`。
3. 手转电机，`h` 的 `hall` 应在 6 个状态间循环（1→3→2→6→4→5 或反序）。

### 阶段 B：只验证功率桥（仍不接电机，示波器看 PA8/PA9/PA10）

1. 发送 `s`：`Start: align rotor...`，此时 EN_GATE 拉高、三相 50% 占空比，相电压平均值为 Vbus/2，**没有差压**。
2. 示波器应看到 20 kHz、50% 的三路 PWM。
3. 发送 `s` 停止，`Stopped.` 后 EN_GATE 拉低。

### 阶段 C：接电机，低压起步

1. **把电源限流调到 0.5~1 A**，先 12 V。
2. 发送 `s`：
   - 前 300 ms 为 **对齐**（频率 0，调制比缓慢上升，电机会“咯噔”一下锁住一个位置）；
   - 然后频率以 4 Hz/s 升到 3 Hz，电机应低速平稳旋转。
3. 若电流过大，软件会把调制比自动回退（foldback，>3 A 开始），超过 6 A 连续 5 次采样则**立即关 EN_GATE** 并报 `*** FAULT: OVERCURRENT ***`；TP181 总电流 |It| > 5.5 A 连续 20 次采样也会独立跳闸（`OVERCURRENT(BUS)`）。
4. 若转向反了，交换任意两根电机线即可（开环 V/f 不支持反转命令）。
5. 逐步用 `+` 升频、`a` 升调制比；每次只改一个量。

实时电流估算：`I = (V_SO − 1.65 V) / (0.02 Ω × 10) = (V_SO − 1.65) / 0.2`，
ADC 分辨率约 **4.0 mA/count**。

母线总电流（TP181）：`It = (1.65 V − V_PA2) / (0.005 Ω × 50) = (1.65 − V_PA2) / 0.25`，
量程 **±6.6 A**，分辨率约 **3.2 mA/count**，状态行显示为 `It=0.42A`。
C34（22 µF）把 20 kHz 纹波旁路掉，所以 `It` 是**平均输入电流**，只用于显示和后备保护，
不能替代 DRV8301 的相电流做 FOC。

---

## 7. 安全设计说明

- **EN_GATE 低 = 全部栅极关断**。空闲、停止完成、任何故障都会先把 EN_GATE 拉低。
- DRV8301 完整 EN_GATE 低电平（>20 µs）会复位寄存器并关闭电流放大器，所以：
  - 空闲时 `drv=off`、相电流显示 `I=--`（`It=` 仍有效，TP181 由 3.3 V 独立供电）；
  - **每次 `s` 启动都会重新写 CR1/CR2 并回读校验**，校验不过就不给 EN_GATE。
- 3-PWM 模式：只驱动 INH（PA8/9/10），INL（PB13/14/15）固定为低。DRV8301 内部插入死区（RDTC = 1 Ω ≈ 50 ns），并且器件本身有独立的直通保护。
- 启动前检查：Vbus ≥ 8 V 且 ≤ 60 V，否则拒绝启动。
- 调制比上限 0.60：保证低侧导通窗口 ≥ 10 µs，ADC 采样（5 通道约 4.4 µs）始终落在有效窗口内。
- ADC 由 PWM 三角波顶点（OVF，低侧导通）触发；若 100 ms 没有 ADC 数据，运行中会直接报 `ADC no data` 并关断。
- 过流两级：软件 foldback（>3 A 降低调制比）+ 硬关断（>6 A，连续 5 次）；DRV8301 自身 OCP 约 12 A 作为最后一道保护。

---

## 8. 常见问题

| 现象 | 排查 |
| --- | --- |
| 无任何串口输出 | 16 MHz 晶振、CN1 接线、115200、TX/RX 是否交叉 |
| `SPI FAILED` 且读回全 `FFFF` | MISO 一直是高 = DRV 根本没驱动 SDO。见下面“SPI 全 F 排查” |
| `PVDD_UV` 一直存在 | 电源电压低于 6 V 或限流触发 |
| 启动立即 `OVERCURRENT` | 电机电阻太小或调制比太大：先 `z` 降到 0.04 再 `s` |
| 电机不转但电流很大 | 相序/霍尔无关（开环），多为 V/f 太低带不动：`a` 增加调制比 |
| 电机抖动/失步 | 升频太快或负载太重：降低频率、增大调制比，或延长 `FREQ_SLEW_UP` |
| `nFAULT` 低 | `d` 看 SR1；过温/过流/欠压按位显示 |

### SPI 读回全 `FFFF` 的排查（CR1/CR2 都等于 FFFF）

`FFFF` 不是“寄存器值错”，而是 16 个时钟里 MISO 从未被拉低 —— DRV8301 没有驱动 SDO。

**先查软件（本工程踩过的坑）**：PA13/PA14/PA15/PB3/PB4 复位后是 JTAG/SWD 功能，
`PSPCR.SPFE[4:0] = 0x1F`，而且 **SPFE 的优先级高于 `PFSR.FSEL`**（参考手册 9.4.7 / 9.4.12）。
所以只调用 `GPIO_Init()` 把 PA15 配成输出是**无效的**：引脚仍被 JTDI 占用，nSCS 永远拉不低，
DRV8301 就永远不会输出 SDO，读回必然全 `FFFF`。`GpioInit()` 开头必须先释放 JTAG：

```c
GPIO_SetDebugPort(GPIO_PIN_TDI | GPIO_PIN_TDO | GPIO_PIN_TRST, DISABLE);
```

（只释放 TDI/TDO/TRST，保留 SWCLK/SWDIO，调试口仍然用 SWD。）PB3/PB4 上的 KEY2/KEY3 同理。

**再查硬件**（`e` = 让 EN_GATE 持续为高，`t` = 慢速重读）：

1. 串口按 `e`（EN_GATE 保持高，三相 50 % 无转矩），万用表量 U1（HTSSOP-56，pin 1 有圆点）：
   - pin 16 EN_GATE = 2.9~3.3 V（若 0 V：PB9→U1-16 断线/虚焊）
   - pin 49 VDD_SPI = 3.3 V
   - pin 23 DVDD = 3.0~3.6 V
   - pin 27 AVDD = 5.5~7.0 V
   - pin 13 GVDD = 8~16 V（EN_GATE 高时电荷泵工作）
   - pin 29/53/54 PVDD = Vbus
   若 DVDD/AVDD/GVDD 都没有，而 PVDD/VDD_SPI 正常：U1 焊接不良或已损坏。
2. 按 `t` 用 390 kHz 慢速重读；仍是 `FFFF` 就排除时钟/时序问题（SPI2 现在由硬件产生 SCK/MOSI，并自动采样 MISO）。
3. 检查 U1 的 pin 8(nSCS)、9(SDI)、10(SDO)、11(SCLK) 是否有虚焊/连锡；HTSSOP-56 0.5 mm 间距很容易连锡。
4. 有示波器就抓上电瞬间：PA15/PC10/PC12 应有 4 组、每组 2 帧 ×16 时钟的波形；U1-10(SDO) 应随之下跳。
5. 调试口用 **SWD**（PA13/PA14）。PA15 的 JTDI 已在 `GpioInit()` 里关掉；若换回旧代码或新建工程，记得保留上面那句 `GPIO_SetDebugPort(...)`，否则 nSCS 仍然无效。
6. 万用表量 U1-6(nFAULT)：EN_GATE 低时 nFAULT 本来就是低（未就绪），不能据此判断好坏。

---

## 9. 后续（本工程不包含）

- 闭环 FOC（Clarke/Park、电流环、速度环、无感观测器）
- 编码器/霍尔换相、参数辨识
- 制动/能量回馈

本工程只负责把“芯片 + 功率桥 + 电机”验证到可用状态。
