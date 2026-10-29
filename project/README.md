# RunRTOS

RunRTOS 是一个面向单片机的轻量、可裁剪、模块化实时操作系统。项目以
STM32F103RBT6 为首发平台，目标是在保持内核确定性、驱动框架和诊断能力的同时，
通过清晰的架构与 SoC 移植边界扩展到更多 MCU。

- GitHub 仓库：https://github.com/WangLei2003/RunRTOS
- 0.1 发布：https://github.com/WangLei2003/RunRTOS/releases/tag/v0.1.0

## 当前状态

项目已完成 `0.1.0` 本地软件与 STM32F103RBT6 板端验收。当前基线包含：

- C17、freestanding、无 libc/CMSIS/ST HAL 依赖的运行时；
- RunRTOS 自有类型、编译器与字符串规范；
- 动态内存池、固定优先级抢占内核和同步对象；
- Cortex-M3 与 STM32F103RBT6 板级移植；
- GPIO、UART、SPI、I2C 设备框架；
- 主机单元测试、板端自检和 Shell。

所有函数定义均须带紧邻的用途注释；该规则由 CMake 配置阶段自动检查，
详见 `docs/standards/coding-style.md`。

## 快速开始

以下命令均从 `project` 目录执行：

```powershell
cd D:\CodexWorkspace\RunRTOS\project
```

构建主机测试：

```powershell
cmake --preset host-tests
cmake --build --preset host-tests
ctest --preset host-tests --output-on-failure
```

构建 STM32F103RBT6 调试固件：

```powershell
cmake --preset f103rb-debug
cmake --build --preset f103rb-debug
```

烧录：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/flash.ps1 -Preset f103rb-debug
```

串口配置为板载 CMSIS-DAP 虚拟串口，`115200 8N1`。

## 目录

- `project/`：RTOS、驱动、板级支持、示例、测试、构建入口、构建产物和设计文档。
- `cache/`：参考 PDF、临时文件和工具缓存，不进入 Git。
- `prompt_and_task/`：每次迭代的原始 Prompt、计划、决策与验证记录。

## 工程规范

分层目录、依赖方向和命名规则以
[`docs/standards/`](docs/standards/README.md) 为唯一强制入口。新增模块、公共接口
或芯片移植前必须先选择明确层级和组件目录。

## 许可

本项目采用 Apache License 2.0，详见 [LICENSE](LICENSE)。

