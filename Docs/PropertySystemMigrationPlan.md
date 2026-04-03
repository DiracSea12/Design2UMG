# Property System Migration Plan

## Goal

- 继续把 `FUMGWidgetNode` 从 legacy `Properties` 字符串包迁移到 typed schema，但保持零回退优先，不做一次性大拆。

## Current State

- 已完成 typed 区域：
  - `TextStyle`
  - `WidgetStyle`
  - `TextData`
  - `Brush`
  - `Brush.Image`
  - `Brush.Gradient`
  - `Effect`
  - `Path`
  - `Shadow`
  - `Render`
  - `WidgetState`
- 当前仍大量依赖 legacy `Properties` 的区域：
  - 若干 applier / generator 消费端仍通过 `BuildEffectivePropertyBag` 走兼容壳
  - 非 typed widget-specific 字段（如部分 Button / ScrollBox / ProgressBar 参数）
  - Button / SizeBox / ScaleBox 等 widget-specific 公共属性

## Strategy

1. 新字段继续“typed first”，不再新增 legacy key。
2. 旧字段按消费链分批迁移，优先迁移高复用、低歧义字段。
3. 只有当读取端和 JSON 往返都稳定后，才删除 legacy 写入。

当前已 admitted 的 cleanup batch 见 `Docs/IncrementalCodeSimplificationBatches.md`。
该台账会固定每一批允许修改的模块、required guard set，以及对应的 admissible full-run 证据。

## Why Not Full Rewrite Now

- 当前项目验证门槛是 baseline 不能退。
- `widget-specific` 长尾字段仍分散在各个 applier，强拔会放大回归面。
- 因此本轮只把 audit 要求里可独立落地且可自动验证的部分继续推进，不把属性系统重构硬塞成一次性工程。
- `legacy / compatibility` 链删除必须单独显式决策，不能作为 routine cleanup 顺手混进 typed-first 收口。

## Verification

- `Design2UMG.PropertyBag.EmptyStringApplies`
- `Design2UMG.UMGJson.TypedBrushImageGradientRoundTrip`
- `Design2UMG.VisualMapper.WrapWithBorderDirect`
- `Design2UMG.VisualMapper.WrapWithBorderTyped`
- `Design2UMG.UMGJson.InnerShadowEffectRoundTrip`
- `Design2UMG.FigmaParser.EffectsConvertToTypedWrappers`
- `Design2UMG.Converter.FigmaPrototypeNavigation`
- `Design2UMG.WidgetBlueprint.FigmaPrototypeNavigationRuntime`
