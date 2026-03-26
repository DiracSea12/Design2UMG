# Property System Migration Plan

## Goal

- 继续把 `FUMGWidgetNode` 从 legacy `Properties` 字符串包迁移到 typed schema，但保持零回退优先，不做一次性大拆。

## Current State

- 已完成 typed 区域：
  - `TextStyle`
  - `Brush`
  - `Effect`
  - `WidgetState`
- 当前仍大量依赖 legacy `Properties` 的区域：
  - 通用 widget 公共属性
  - Image / Material 参数
  - 一部分 text / layout / wrapper glue 属性

## Strategy

1. 新字段继续“typed first”，不再新增 legacy key。
2. 旧字段按消费链分批迁移，优先迁移高复用、低歧义字段。
3. 只有当读取端和 JSON 往返都稳定后，才删除 legacy 写入。

## Why Not Full Rewrite Now

- 当前项目验证门槛是 baseline 不能退。
- `Image / Material / Layout` 仍广泛依赖 legacy key，强拔会放大回归面。
- 因此本轮只把 audit 要求里可独立落地且可自动验证的部分继续推进，不把属性系统重构硬塞成一次性工程。

## Verification

- `Design2UMG.UMGJson.InnerShadowEffectRoundTrip`
- `Design2UMG.Converter.FigmaPrototypeNavigation`
- `Design2UMG.WidgetBlueprint.FigmaPrototypeNavigationRuntime`
