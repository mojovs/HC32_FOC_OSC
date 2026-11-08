# HC32_FOC_OSC

HC32 系列 MCU 的 FOC 电机控制工程。

当前仓库收录 **PC 上位机工具**（`smdrive-host/`），用于电机驱动器的调试与标定：串口波形观测、
参数在线下发、数据/原始流留档与离线回放。固件部分后续补齐。

| 路径 | 说明 |
|---|---|
| `smdrive-host/` | PC 上位机（Qt 5.15.2 / MSVC2019，C++20）。串口 + 协议层、实时波形与数据表、参数下发与方案管理、CSV/原始流记录与回放、拔插重连、windeployqt + Inno Setup 打包 |

## 上位机快速开始

```bat
cd smdrive-host
build.bat            :: 配置 + 编译            -> build\smdrive-host.exe
build.bat test       :: 编译 + 协议单元测试     -> 100% tests passed
build.bat deploy     :: windeployqt 出可分发目录 -> dist\
packaging\make_installer.bat  :: 出安装包        -> installer\*.exe
```

依赖：Qt 5.15.2（MSVC2019 x64 kit）、Visual Studio 2019、VS 自带的 CMake + Ninja。
构建脚本里的工具链路径按需修改；无硬件时可用 `--replay` 回放留档的原始流验证全链路。

详见 [`smdrive-host/README.md`](smdrive-host/README.md)。
