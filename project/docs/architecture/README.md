# 架构与目录约束

RunRTOS 使用显式分层隔离依赖，并允许通过 CMake 目标裁剪尚未使用的模块。

```text
core
  -> runtime
  -> kernel
  -> arch / SoC port
  -> drivers
  -> bsp
  -> components / applications
```

公共头文件位于 `include/runrtos/<layer>/`，实现位于 `src/<layer>/<component>/`。
跨层引用必须使用 `runrtos/<layer>/<header>.h` 路径；仅同一模块内部允许相对包含。
架构、芯片厂商头文件和板卡资料不得泄漏到内核或通用驱动接口中。

