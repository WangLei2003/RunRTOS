# 分层与目录规范

## 强制依赖方向

RunRTOS 分为以下层，箭头表示唯一允许的依赖方向：

```text
core -> runtime -> kernel -> arch/SOC -> drivers -> bsp -> components/applications
```

1. `core` 只能依赖编译器预定义设施，不得依赖其他 RunRTOS 层。
2. `runtime` 可以依赖 `core`，不得依赖内核、架构、驱动或板级代码。
3. `kernel` 可以依赖 `core` 和 `runtime`，只能通过 `arch` 抽象接口访问硬件。
4. `arch` 实现架构接口；SOC 移植代码不得反向包含内核私有实现以外的上层模块。
5. `drivers` 暴露通用设备模型和能力接口，不得向应用泄漏芯片寄存器。
6. `bsp` 负责具体板卡的时钟、引脚、设备装配和板级启动。
7. `components` 与 `applications` 只能消费下层公开接口。

禁止同层无边界互相依赖、下层包含上层头文件、跨层直接访问私有结构体或通过隐式
外部符号耦合。CMake 目标依赖必须与源码依赖方向一致。

## 目录规范

```text
project/
  include/runrtos/
    core/
    runtime/
    kernel/
    arch/
    drivers/
      gpio/
      uart/
      spi/
      i2c/
  src/
    core/<component>/
    runtime/<component>/
    kernel/<component>/
    arch/<architecture>/<component>/
    drivers/<component>/
    bsp/<board>/
    components/<component>/
    applications/<application>/
  tests/
    host/
    target/
  docs/
    architecture/
    adr/
    standards/
  cmake/
  scripts/
```

每个组件必须按职责继续拆分子目录，不允许在层根目录堆放大批实现文件。公共头文件
必须位于 `include/runrtos/<layer>/...`，私有头文件必须位于对应 `src` 组件的
`internal/` 或实现目录内。

## 接口边界

- 公共接口必须使用完整包含路径 `runrtos/<layer>/<component>/<header>.h`。
- 私有结构体、链表节点、状态变量和寄存器布局不得出现在公共头文件中。
- 每个公共对象必须以不透明句柄暴露，例如 `RunThread *`。
- 架构通用接口放 `arch`，Cortex-M 实现放 `arch/cortex_m3`，芯片差异放 SOC/BSP。
- 驱动接口按能力拆分；厂商实现不得污染公共操作表。
- 新增可选模块必须提供独立 CMake 目标，未启用时不得参与编译或链接。
- 板卡、芯片和第三方资料只存放在工作区 `cache/reference`，不得进入源码目录。

## 文件放置判定

新增文件前必须回答以下问题：

1. 它属于哪个架构层，而不是它当前被谁调用？
2. 它是公共契约还是实现细节？
3. 是否可按芯片、板卡或功能独立裁剪？
4. 如果删除该模块，上层哪些目标允许因此失去功能？

无法明确归类时不得直接放在层根目录，必须先设计组件边界或提交 ADR。

