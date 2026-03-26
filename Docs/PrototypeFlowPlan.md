# Prototype Flow Plan

## Goal

- 把当前已落地的 `WidgetSwitcher + OnClick -> NavigateTo` 主链继续稳定为可维护的 prototype 基础闭环。
- 明确“哪些交互已经进入运行时主链，哪些仍刻意不做”。

## Current State

- 已完成：
  - 顶层多页面 Frame -> `WidgetSwitcher`
  - `ON_CLICK + NAVIGATE/SWAP`
  - 基础 transition：fade / slide，保留 `type / direction / duration / easing`
- 未完成：
  - overlay
  - back stack / history
  - hover / drag / keyboard trigger
  - 条件跳转与变量驱动 flow
  - 完整 `UWidgetAnimation` 资产生成

## Next Milestones

1. 把 overlay / open link / back 这类高 ROI action 纳入 typed binding，而不是继续读 metadata。
2. 为 prototype 运行时扩展增加更强的冲突策略，例如动画中再次点击的排队或中断语义。
3. 若后续确实需要资产级动画，再单独评估 `UWidgetAnimation` 生成链，不和当前最小闭环混做。

## Verification

- `Design2UMG.Converter.FigmaPrototypeNavigation`
- `Design2UMG.WidgetBlueprint.FigmaPrototypeNavigationRuntime`
- `Design2UMG.WidgetBlueprint.FigmaPrototypeSlideTransitionRuntime`
