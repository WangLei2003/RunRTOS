# 目录分层、构建位置与工程规范

- 时间：2026-10-29 23:20:08 +08:00
- 主题：工作区根目录约束、公共头文件分层、源码分层、命名规范

## 原始 Prompt

> 构建目录也写在D:\CodexWorkspace\RunRTOS\project下

> D:\CodexWorkspace\RunRTOS\project\include\runrtos目录下怎么有各种文件？你应该每一层都做好分级呀，src也是

> 严格的分层分级架构设计与代码命名，都得写入本rtos的规范中

## 助手计划

1. 工作区根目录只保留 `cache`、`project`、`prompt_and_task`，所有工程文件和构建
   产物进入 `project`。
2. 公共头文件按 `core`、`runtime`、`kernel`、`arch`、`drivers`、`bsp`、
   `components`、`applications` 分层，层内继续按组件拆分。
3. 源码采用相同分层，私有头文件只保存在对应实现目录。
4. 将依赖方向、目录放置、公共接口边界、命名表、标准库限制和评审门槛写入
   `project/docs/standards`。
5. 在 CMake 配置阶段自动检查禁用标准头和跨层反向依赖。
6. 迁移所有包含路径和 CMake 目标，验证 host、debug、release、minimal 构建。

## 已确认决策

- 公共包含路径统一为 `runrtos/<layer>/<component>/<header>.h`。
- 源码依赖方向强制为
  `core -> runtime -> kernel -> arch -> drivers -> bsp -> components/applications`。
- 公共对象是不透明句柄，公共头文件不得暴露实现结构。
- 函数使用 `run_snake_case`，宏使用 `RUN_UPPER_SNAKE_CASE`，类型使用
  `RunCamelCase`，成员和局部变量使用 `runCamelCase`。
- 构建目录统一位于 `project/build/<preset>`。
- 标准头、厂商 HAL、CMSIS 和第三方运行时源码不得进入固件运行时。
- 规范例外必须通过 ADR 明确范围、理由和退出条件。

## 实施结果

- 根目录清理为 `cache`、`project`、`prompt_and_task` 三项。
- `.vscode/settings.json` 迁入 `project/.vscode`。
- 公共头文件已迁移到 `project/include/runrtos/{core,runtime,kernel,arch,drivers,bsp,applications}`。
- 源码已迁移到 `project/src/{core,runtime,kernel,arch,drivers,bsp,applications}`，
  并继续按组件拆分为子目录。
- 已删除空的旧 `project/src/lib` 目录。
- 已新增 `project/docs/standards/` 下的架构、命名和实现规范，以及目录决策 ADR。
- 已新增 `project/cmake/check-standards.cmake`，在 CMake 配置阶段拒绝标准头、
  公共头相对包含和跨层反向依赖。
- 已建立 core、runtime、kernel、arch、drivers、SOC driver、BSP 和 application
  独立 CMake 目标。

## 验证结果

- `host-tests`：构建成功，CTest `1/1` 通过。
- `f103rb-debug`：构建成功，text `22128` 字节，bss `10640` 字节。
- `f103rb-release`：构建成功，text `17596` 字节，bss `10640` 字节。
- `f103rb-minimal`：构建成功，text `13148` 字节，bss `10640` 字节。
- release ELF 无未解析符号；仅发现允许的 GCC 算术辅助符号
  `__aeabi_uldivmod`、`__aeabi_idiv0`、`__aeabi_ldiv0`。
- 所有构建目录位于 `project/build`。

## 未决事项

- 需要在实际 STM32F103RBT6 板上验证时钟、LED、USART1、调度、软件定时器和
  故障路径。
- GPIO 中断、UART/SPI/I2C 异步完成回调尚需补齐。
- GitHub CLI 登录、私有仓库创建、Git 首次提交和远端推送仍需在用户完成浏览器
  授权后继续。

## 追加记录：文件头与代码注释

- 时间：2026-10-29 23:29:32 +08:00
- 主题：统一源码文件头、公共 API 契约和生产代码中的关键算法注释

### 原始 Prompt

> 我看了已有的代码，我希望你可以写上文件头注释与代码注释，以便我阅览

### 助手计划

1. 保留每个手写源码、头文件、汇编和 CMake 文件开头的 SPDX 标识，并补齐
   Layer、Purpose、Dependencies、Constraint 等职责边界。
2. 为公共 API 说明调用上下文、所有权、阻塞/超时、返回状态和并发约束。
3. 在内核、分配器和硬件后端中解释位图、时间回绕、等待队列、优先级继承、
   栈帧布局、外设时序和魔数常量，不逐行复述代码。
4. 把文件头完整性加入配置期自动检查，避免后续新增文件遗漏。
5. 重新执行主机测试和三套 F103 固件构建，核对尺寸与链接符号。

### 实施结果

- 公共线程、同步、内存、字符串、格式化、设备和 GPIO/UART/SPI/I2C 接口已补齐
  API 契约注释。
- 内核调度、超时链表、等待唤醒、线程删除、上下文切换、互斥量优先级继承、
  队列重新检查、事件组广播和软件定时器行为已增加原因型注释。
- TLSF 思路分配器的分级位图、最小拆分、相邻合并和空闲链表复用已增加注释。
- Cortex-M3 PendSV、启动代码、故障帧、PRIMASK 保存、NVIC 和 DWT 操作已注明
  硬件副作用与 ABI 约束。
- F103 时钟、LED、USART1、I2C1、SPI2 和 SysTick 配置中的关键位值已改为命名
  常量，并补充引脚、时序及外设限制说明。
- 所有手写 CMake 文件头已补齐 Layer、Purpose、Dependencies 和 Constraint。
- `check-standards.cmake` 现会拒绝缺少 SPDX、Purpose/职责或 Constraint/约束的
  C、头文件和汇编文件。
- 审阅 Cortex-M3 优先级初始化时，将 PendSV 和 SysTick 都设为最低可配置优先级，
  确保 PendSV 只在内核临界区和更高优先级异常结束后执行。

### 验证结果

- `host-tests`：配置和构建成功，CTest `1/1` 通过。
- `f103rb-debug`：构建成功，text `22128` 字节，bss `10640` 字节，零警告。
- `f103rb-release`：构建成功，text `17596` 字节，bss `10640` 字节，零警告。
- `f103rb-minimal`：构建成功，text `13144` 字节，bss `10640` 字节，零警告。
- release ELF 的 `nm -u` 输出为空，不存在未解析符号。
- 文件头审计未发现缺项；受检源码、头文件、汇编和 CMake 文件均无超过 100 列的行。

### 未决事项

- 本轮只完成注释和静态验证，尚未烧录开发板，因此 LED、COM5 串口、时钟和
  PendSV 优先级调整仍需要板端复核。
- 软件定时器回调的并发删除限制已在 API 注释中明确，后续应通过服务线程状态机
  消除“回调执行期间外部删除定时器”的竞态。

