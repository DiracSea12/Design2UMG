# Prototype Flow Plan

## Goal

- 把当前已落地的 `WidgetSwitcher + typed trigger/action runtime contract` 主链继续稳定为可维护的 prototype 基础闭环。
- 明确“哪些交互已经进入运行时主链，哪些仍刻意不做”。

## Product Scope

- `Prototype Basic` 作为插件正式支持能力对外口径发布；它的目标是把设计稿里的页面切换、基础触发器、基础过渡和轻量条件流稳定导出到 UMG。
- 正式支持范围限定为：`WidgetSwitcher` 页面收口、typed trigger/action runtime contract、`OVERLAY/BACK/OPEN_LINK` 轻量运行时 contract、基础 fade / slide 过渡。
- `Prototype Basic` 不是“完整 Figma Prototype 复刻”承诺，也不是通用状态机 / 事件图编译器。
- 完整 overlay layer semantics、完整 Smart Animate 图层匹配、通用动画 authoring、广义交互图状态机继续视为扩容项，而不是当前正式承诺。
- 本文档描述的是 `.pen` / `Figma Design` 导入后的运行时 contract；live MCP sync 本身不属于 `Prototype Basic` 的正式承诺范围。

## Current State

- 已完成：
  - 顶层多页面 Frame -> `WidgetSwitcher`
  - `ON_CLICK + NAVIGATE/SWAP`
  - `ON_HOVER / ON_PRESS` 触发器进入 typed runtime contract
  - `ON_DRAG / ON_KEY_DOWN / ON_KEY_UP` 触发器进入 typed runtime contract
  - `ON_CLICK + OVERLAY`
  - `ON_CLICK + BACK`
  - `ON_CLICK + OPEN_LINK`
  - `SET_VARIABLE / SET_VARIABLE_MODE / CONDITIONAL` 进入 typed step program，并在运行时按顺序执行
  - 基础 transition：fade / slide，保留 `type / direction / duration / easing`
  - 为切页过渡生成最小两段式 `UWidgetAnimation` 资产（`out` / `in`），runtime extension 优先播放生成动画
  - 以上 action 统一进入 typed `PrototypeNavigation.ClickBindings`，不再额外依赖 metadata 补丁
  - 当前 shipped prototype 主链已经稳定为“页面切换 + 运行时近似过渡”，并作为 `Prototype Basic` 正式支持范围对外收口
- 未完成：
  - 更完整的 prototype variable / mode 生态，而不是仅服务于当前 runtime contract 的轻量状态存储
  - 完整通用 `UWidgetAnimation` authoring / Smart Animate layer matching

## Current Boundaries

- 当前 overlay 已提升为 `base switcher + overlay host` 双层 contract：
  - 会把 overlay 页临时挂到独立 `Overlay` 容器，并维护 overlay stack / back 恢复；
  - 但仍不生成独立浮层锚定、遮罩点击关闭或通用 dismiss/placement contract。
- 当前 back 使用运行时 history stack：
  - 只覆盖本次 prototype runtime contract 下的页面回退，不是完整图交互状态机。
- 当前 open link 直接走运行时 URL 打开：
  - 自动化里通过 hook 捕获 URL；正式运行时由平台 `LaunchURL` 执行。
- 当前 keyboard trigger 走焦点约束下的输入路由，conditional flow 走 typed step program：
  - 这已经覆盖主线 prototype 闭环；
  - 但它仍不是通用交互状态机或图表级事件生成链。
- 本轮不做完整通用 `UWidgetAnimation` authoring：
  - 当前已经为切页 fade / slide 生成最小两段式动画资产；若要继续走到通用动画 authoring、事件轨或 Smart Animate 图层配对，仍需要额外蓝图/动画生成链。

## Next Milestones

1. 为 prototype 运行时扩展增加更强的冲突策略，例如动画中再次点击的排队或中断语义。
2. 若需要更接近设计器语义的 overlay，再单独评估独立浮层容器、dismiss 行为和锚定 contract。
3. 若后续确实需要通用动画 authoring，再单独扩展当前最小 `UWidgetAnimation` 生成链，不和当前切页闭环混做。

## Verification

- `Design2UMG.Converter.FigmaPrototypeNavigation`
- `Design2UMG.Converter.FigmaPrototypeSpecialActions`
- `Design2UMG.Converter.FigmaPrototypeHoverAndPress`
- `Design2UMG.Converter.FigmaPrototypeKeyboardAndConditional`
- `Design2UMG.WidgetBlueprint.FigmaPrototypeNavigationRuntime`
- `Design2UMG.WidgetBlueprint.FigmaPrototypeSpecialActionsRuntime`
- `Design2UMG.WidgetBlueprint.FigmaPrototypeHoverAndPressRuntime`
- `Design2UMG.WidgetBlueprint.FigmaPrototypeAdvancedTriggersRuntime`
- `Design2UMG.WidgetBlueprint.FigmaPrototypeSinglePageOpenLinkRuntime`
- `Design2UMG.WidgetBlueprint.FigmaPrototypeSlideTransitionRuntime`
