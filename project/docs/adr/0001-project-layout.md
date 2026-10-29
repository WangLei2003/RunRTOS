# ADR 0001：分层目录与构建产物

- 状态：已接受
- 日期：2026-10-29
- 决策：`include/runrtos` 与 `src` 均按 `core`、`runtime`、`kernel`、`arch`、
  `drivers`、`bsp`、`components`、`applications` 分层；各层内部继续按职责建立
  子目录。
- 决策：所有 CMake 构建目录位于 `project/build/<preset>`。
- 原因：限制依赖方向、方便按目标裁剪，并避免工程文件散落到工作区根目录。

