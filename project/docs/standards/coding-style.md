# 代码与命名规范

## 文件和包含

- C 源文件使用小写下划线，不使用与标准库重名的文件名。
- 公共头文件使用 `run_<component>.h`，并置于分层目录。
- 私有头文件使用 `run_<component>_internal.h`，仅留在对应 `src` 目录。
- 公共头文件必须包含 SPDX 标识、包含保护和完整 `runrtos/...` 包含路径。
- 一个头文件只暴露一个清晰组件边界，不得汇总无关模块。

每个手写源码、头文件、汇编和 CMake 文件必须以文件头注释开始。C/汇编文件使用
以下结构；CMake 文件使用对应的 `#` 注释：

```c
/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - 组件名称
 *
 * 层：core/runtime/kernel/arch/drivers/bsp/components/applications
 * 职责：该文件解决的单一问题
 * 依赖：允许依赖的层或外部符号
 * 约束：调用上下文、所有权、时间或并发限制
 */
```

文件头不能只重复文件名。缺失职责或依赖说明的头文件不得合入。

## 标识符

| 对象 | 规则 | 示例 |
| --- | --- | --- |
| 公共函数 | `run_snake_case` | `run_thread_create` |
| 中断安全函数 | `run_<action>_from_isr` | `run_queue_send_from_isr` |
| 宏 | `RUN_UPPER_SNAKE_CASE` | `RUN_STATUS_OK` |
| 公共类型 | `RunCamelCase` | `RunThreadConfig` |
| 不透明对象 | `Run<Object>` | `RunSemaphore` |
| 全局对象 | `RunCamelCase` | `RunKernel` |
| 结构体成员 | `runCamelCase` | `runStackSize` |
| 函数参数/局部变量 | `runCamelCase` | `runTimeout` |
| 静态文件变量 | `RunCamelCase` | `RunTimerFirst` |
| 枚举值 | `RUN_UPPER_SNAKE_CASE` | `RUN_THREAD_READY` |
| 内部辅助函数 | `run_<component>_<action>` | `run_kernel_ready_insert` |

控制流关键字必须保持 C 原样。不得使用匈牙利前缀、单字母标识符或仅靠大小写区分
含义的命名。名称必须表达领域含义，禁止无意义缩写。

## 类型和接口

- 公共代码禁止使用 `char`、`int`、`long`、`float`、`double` 等原生基础类型；
  必须使用 `RUN_CHAR`、`RUN_INT_32`、`RUN_UINT_32`、`RUN_FP_32`、`RUN_SIZE`、
  `RUN_VOID`、`RUN_BOOL` 等 RunRTOS 类型。
- 函数返回 `RunStatus`；数据通过明确的输出参数返回。
- 输入参数使用 `run` 小驼峰，输出参数必须带 `Out` 后缀。
- 公共对象结构体不得定义在公共头文件中，只能通过句柄访问。
- 二进制数据接口统一使用“指针 + 长度”，禁止依赖空字符终止的隐式长度。
- 字符串输入可使用 `RunStringView` 表示非拥有视图，拥有对象使用 `RunString`。
- 普通阻塞 API 与 `*_from_isr` API 必须分离，并在命名和文档中明确调用上下文。

## 代码格式

- 使用 C17，固定四空格缩进，左花括号另起一行。
- 每行原则上不超过 100 列。
- 每个函数定义前必须紧邻一行用途注释，说明该函数承担的单一职责。该规则适用于
  公共头文件中的内联函数、主机测试函数和汇编入口；函数原型由所在组件接口统一
  说明，不以原型替代定义处注释。
- C 函数用途注释使用 `/* ... */`，汇编入口使用 `@ ...`，均不得与函数签名之间
  插入空行或其他语句。多行签名时注释放在签名第一行之前。
- 关键不变量、回绕比较、寄存器副作用和内存所有权必须写明理由。
- 注释解释“为什么、约束和失败方式”，禁止逐行复述赋值或循环语句。
- 每个公共 API 必须说明调用上下文、所有权、阻塞行为和错误语义。
- 每个非直观状态转换、位图运算、上下文帧和寄存器序列必须有局部注释。
- 禁止提交注释掉的旧实现、调试输出和无说明的魔数；寄存器值必须定义为命名常量。
- 编译必须零警告；不得通过降低警告级别掩盖新问题。

