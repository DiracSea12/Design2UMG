# Effect Pipeline Plan

## Goal

- 统一 subtree effect 的运行时 contract，避免 blur / inner shadow / 局部后处理继续分散成多套特判逻辑。

## Current State

- 已完成：
  - `foreground blur` -> `RetainerBox + EffectMaterial`
  - `inner shadow` -> `RetainerBox + EffectMaterial`
  - `background blur` -> `UBackgroundBlur`
  - 本地 Figma `layer blur` 导入期 bake
  - 当前 shipped effect 参数优先进入 typed `Effect` 子树；同名 legacy `Properties` 只保留兼容兜底
- 未完成：
  - 更高保真的 blur / inner shadow 语义
  - 多层 effect 组合策略
  - 更高保真 glow / blend / mask pipeline

## Current Boundaries

- 本轮不做 foreground blur / inner shadow 的“完整设计器语义等价”：
  - 当前运行时实现仍基于 `RetainerBox + EffectMaterial` 单通道近似，缺少多 pass / 自定义 Slate / 后处理图的基础设施。
- 本轮不做更广义 glow / blend / mask pipeline：
  - 这会扩张到新的 wrapper contract、材质参数协议和导入链，不属于这轮 typed/property 收口。

## Next Milestones

1. 新增 effect 字段继续走 typed `Effect`，不要回写新的 legacy key。
2. 把更多可维护的局部视觉能力复用到现有 retainer contract，而不是新增平台分支。
3. 为 effect wrapper 建立更强的像素级 fixture，单独验证 blur/shadow/material 参数。

## Verification

- `Design2UMG.Converter.ForegroundBlurRetainerWrapper`
- `Design2UMG.Shadow.InnerShadowRetainerWrapper`
- `Design2UMG.BatchImport.FigmaLocalLayerBlurBake`
