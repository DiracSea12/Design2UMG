# Effect Pipeline Plan

## Goal

- 统一 subtree effect 的运行时 contract，避免 blur / inner shadow / 局部后处理继续分散成多套特判逻辑。

## Current State

- 已完成：
  - `foreground blur` -> `RetainerBox + EffectMaterial`
  - `inner shadow` -> `RetainerBox + EffectMaterial`
  - `background blur` -> `UBackgroundBlur`
  - 本地 Figma `layer blur` 导入期 bake
- 未完成：
  - 更多 effect 的统一 typed contract
  - 多层 effect 组合策略
  - 更高保真 glow / blend / mask pipeline

## Next Milestones

1. 继续把 effect 参数优先写入 typed `Effect`，减少新字段落回 legacy `Properties`。
2. 把更多可维护的局部视觉能力复用到现有 retainer contract，而不是新增平台分支。
3. 为 effect wrapper 建立更强的像素级 fixture，单独验证 blur/shadow/material 参数。

## Verification

- `Design2UMG.Converter.ForegroundBlurRetainerWrapper`
- `Design2UMG.Shadow.InnerShadowRetainerWrapper`
- `Design2UMG.BatchImport.FigmaLocalLayerBlurBake`
