# Documentation Drift Fix Plan

## Goal

- 保证实现、技术文档、限制说明和 OpenSpec 任务口径一致，避免“代码已经变了，文档还停在旧结论”。

## Current State

- 已同步：
  - gradient / angular / multi-stop
  - foreground blur / inner shadow / layer blur
  - Figma node coverage：`TRANSFORM_GROUP`、`TEXT_PATH`、`SLICE` / FigJam-only
  - prototype navigation 与基础 transition
- 仍需持续维护：
  - property migration 的真实边界
  - prototype 范围裁剪
  - 新增测试夹具与基线验证流程

## Working Rules

1. 每次能力边界变化后，同步更新：
   - `Docs/PropertyMapping.md`
   - `Docs/Design2UMG_TechnicalDesign.md`
   - `KNOWN_LIMITATIONS.md`
   - 对应 OpenSpec `design/tasks`
2. 文档写“当前真实能力”，不要写“未来希望能力”。
3. 对近似实现要明确标注“近似/边界”，不要把 partial support 写成 full fidelity。

## Verification

- 文档内容与当前自动化测试、转换结果、运行时行为可交叉印证。
