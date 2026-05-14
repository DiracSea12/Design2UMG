# Design2UMG Roadmap

**Created**: 2026-02-15
**Basis**: code review, competitor research, and three rounds of cross-checking
**Status legend**: `[ ]` not started | `[~]` in progress | `[x]` complete
**Review-driven hardening**: internal review plans are not public release promises. Remaining hardening work is tracked by the corresponding OpenSpec phase.

---

## Strategy: Open-Core Foundation Plus Commercial Upside

### Why open source

- The UE5 "design to UMG" space is still thin. Existing references include Figma2UMG (29 stars, moved from paid to free) and Figma2Unreal (87 stars, self-described as rudimentary).
- Unity's Figma Converter for Unity ($99, 49 reviews, six years of updates) shows that demand exists, while UE-side willingness to pay is less proven.
- Design2UMG has a more complete architecture than the visible UE competitors; open source can establish the project position quickly.
- The AI design-to-code wave has not fully reached game-engine UI workflows yet, so the window matters.

### Commercial directions to explore after open source

- Pencil MCP / live sync may become a product direction, but the current repository only promises bridge and debug utility behavior.
- Enterprise support and consulting.
- Advanced feature plugins, such as animation-state export and design-system sync.

---

## Competitor Gap

| Capability | Design2UMG | Figma2UMG | Gap |
|------|----------|-----------|------|
| Layout conversion | Flexbox -> Anchors, nine-slice, hysteresis | basic layout | Ahead |
| Visual properties | fill / stroke / radius / shadow / blur | basic fill | Ahead |
| Component system | ref expansion + variables + themes | components / variants | Similar |
| Prototype flow | minimal loop shipped, more scope pending | interaction parsing + Blueprint graph generation | Still behind |
| Gradients | two-color H/V/radial | none | Ahead |
| SVG path | full command rasterization | none | Ahead |
| Multiple input sources | .pen + Figma + MCP bridge/debug utility | Figma only | Ahead |
| Blueprint logic generation | runtime extension first; no full UEdGraph authoring path | UEdGraph nodes | Still behind |

**Conclusion**: the main gaps have moved from "does it exist" to coverage and semantic completeness. Prototype flow plus general animation / graph authoring remain the largest gaps. Figma2UMG's MIT-licensed `WidgetBlueprintHelper` can remain a reference implementation.

---

## Phase 0: Open-Source Readiness

> Goal: clean code and docs enough to meet the open-source release bar.

| Task | File | Status | Notes |
|------|------|------|------|
| **Replace copyright headers** (blocking) | all `.h/.cpp` | [x] | `Diracsea12` MIT copyright header |
| Remove internal references and hard-coded paths | global | [x] | scan complete; no sensitive information found |
| Add LICENSE | LICENSE | [x] | MIT |
| Improve README | README.md | [x] | multiple input sources, architecture diagram, Quick Start, Known Limitations |
| Add CONTRIBUTING | CONTRIBUTING.md | [x] | contributor guide, coding standards, architecture overview |
| Add GitHub Actions CI | .github/workflows/ | [~] | public `repository-hygiene` check and manually triggered self-hosted BuildPlugin workflow exist; this is not a complete public CI matrix |
| Add GitHub Release template | .github/ | [~] | PR and bug-report templates exist; release-notes template is still separate future work |
| Maintain CHANGELOG | CHANGELOG.md | [x] | Keep a Changelog format, v1.0.0 plus Unreleased |
| Update `.uplugin` metadata | Design2UMG.uplugin | [x] | CreatedBy / DocsURL / SupportURL |

---
## Phase 1：性能与质量修复（纯内部优化）

> 目标：修掉已知限制中投入最小、收益最大的问题

### 1.1 Path 栅格化缓存 ✅

**现状**：每次预览刷新都重新调用 `FPenPathRasterizer::Rasterize()` 创建 transient texture，频繁刷新时掉帧、内存增长。

**方案**：以 `Geometry + Width + Height + FillColor + FillRule` 为缓存键，命中直接复用。缓存实现完全封装在 .cpp 匿名命名空间内，header 公开 API 零变化。

| 任务 | 文件 | 状态 |
|------|------|------|
| 定义缓存键结构 `FRasterCacheKey` | PenPathRasterizer.cpp | [x] |
| 实现 `GetTypeHash` | PenPathRasterizer.cpp | [x] |
| 添加 `TMap<FRasterCacheKey, TWeakObjectPtr<UTexture2D>>` 缓存 | PenPathRasterizer.cpp | [x] |
| Rasterize() 入口加缓存查询/写入 | PenPathRasterizer.cpp | [x] |
| 缓存淘汰策略（WeakPtr 自动失效 + LRU 上限 256） | PenPathRasterizer.cpp | [x] |
| Hit/Miss 计数 + `FAutoConsoleCommand` 自注册 `Design2UMG.CacheStats` / `Design2UMG.CacheClear` | PenPathRasterizer.cpp | [x] |

### 1.2 BoxShadow SDF 材质替换 ⏸️ 推迟，且当前不是公开发布承诺

**现状**：Overlay + 高斯模糊 Image 方案，ClipToBounds 下阴影被裁剪，只支持单层外阴影+均匀圆角。CPU 方案无法根治。

**推迟原因**：性价比低。阴影裁剪只在紧凑布局+父级 ClipToBounds 时出现，属于边缘 case。引入材质资产会丢失"纯代码零资产"卖点，9 个子任务工程量大。推迟到社区有需求时再做；在此之前，不能把它写成当前 release 方案。

| 任务 | 文件 | 状态 |
|------|------|------|
| 修改 `CanContainContent: true` | Design2UMG.uplugin | [ ] |
| 创建 `Content/Materials/` 目录 | Content/ | [ ] |
| 设计统一的 SDF 材质架构（参数传递机制、MaterialRef 注册表），预留渐变扩展点 | Content/Materials/ + PenVisualMapper.h | [ ] |
| 实现 `MI_BoxShadow` 材质（SDF + smoothstep 模拟模糊） | Content/Materials/ | [ ] |
| 实现 `MI_RoundedRect` 材质（四角独立圆角 SDF） | Content/Materials/ | [ ] |
| PenVisualMapper 中按需创建 MID 替代 WrapWithBoxShadow | PenVisualMapper.cpp | [ ] |
| 处理 Material Domain = "User Interface" | 材质资产 | [ ] |
| 实现 MID 烘焙到 UTexture2D 的 fallback 路径（卸载插件后不 broken） | UMGGenerator.cpp | [ ] |
| 回归测试：对比替换前后渲染结果 | Tests/ | [ ] |

### 1.3 渐变精度提升 ⏸️ 推迟

**现状**：已有基础实现（`GenerateGradientH/V/RadialGradient`），但只取首尾两色，线性渐变只离散为 H/V 两方向。

**推迟原因**：依赖 1.2 的材质基础设施，1.2 推迟后此项也随之推迟。

| 任务 | 文件 | 状态 |
|------|------|------|
| 实现 `MI_LinearGradient` 材质（多色站点 + 任意角度） | Content/Materials/ | [ ] |
| 实现 `MI_RadialGradient` 材质（多色站点 + 中心偏移） | Content/Materials/ | [ ] |
| PenVisualMapper 渐变分支切换到 MID 方案 | PenVisualMapper.cpp | [ ] |
| 保留 ProceduralShapeGenerator 作为 fallback（材质不可用时） | ProceduralShapeGenerator.cpp | [ ] |

### 1.4 椭圆走 Path 栅格化 ✅

**现状**：~~`cornerRadius = min(w,h)/2` 近似，非正圆时变胶囊形。~~ 已修复。

**方案**：椭圆节点直接生成椭圆 SVG path data（两段半弧），复用已有的 Path 栅格化管线。无填充或无尺寸时 fallback 到圆角近似。

| 任务 | 文件 | 状态 |
|------|------|------|
| 实现椭圆→SVG path 转换函数 | Design2UMGConverter.cpp | [x] |
| Ellipse case 改走 CreatePathWidget 路径 | Design2UMGConverter.cpp | [x] |
| 验证：不同宽高比椭圆的渲染精度 | Tests/ | [ ] |

---

## Phase 2：Prototype Flow（补齐竞品差距）

> 目标：支持设计稿中的页面导航逻辑，追平 Figma2UMG
>
> 参考实现：[Figma2UMG](https://github.com/Buvi-Games/figma2umg)（MIT）
> - `Source/Figma2UMG/Public/Builder/WidgetBlueprintHelper.h` — UEdGraph 节点创建（UK2Node_CallFunction / UK2Node_SwitchString / UK2Node_VariableGet）、Pin 连接、CreateSwitchFunction / PatchSwitchFunction
> - `Source/Figma2UMG/Public/Parser/Nodes/FigmaNode.h` — `FFigmaInteraction`、`EFigmaTriggerType`、`EFigmaActionType`、`EFigmaActionNodeNavigation`、`GetInteractionFromTrigger()`
> - `Source/Figma2UMG/Public/Builder/Widget/WidgetSwitcherBuilder.h` — UWidgetSwitcher 构建与 Patch

### 2.0 当前状态（2026-03-27）

| 能力 | 当前状态 |
|------|------|
| Figma prototype typed parse（interaction / transition / start node） | [x] |
| 多页面 `WidgetSwitcher` 导航 | [x] |
| `ON_CLICK + NAVIGATE/SWAP/OVERLAY/BACK/OPEN_LINK` 最小闭环 | [x] |
| `ON_HOVER / ON_PRESS` 触发器进入 prototype runtime contract | [x] |
| 运行时淡入淡出 / 位移近似过渡 | [x] |
| 切页最小 `UWidgetAnimation` 资产生成（fade / slide） | [x] |
| drag / keyboard / conditional flow | [ ] |
| 独立 overlay layer semantics | [ ] |
| 完整 `UWidgetAnimation` 资产生成 | [ ] |
| 完整 Smart Animate 图层匹配 | [ ] |

> 验证：自动化测试 `Design2UMG.Converter.FigmaPrototypeNavigation`、`Design2UMG.Converter.FigmaPrototypeSpecialActions`、`Design2UMG.Converter.FigmaPrototypeHoverAndPress`、`Design2UMG.WidgetBlueprint.FigmaPrototypeNavigationRuntime`、`Design2UMG.WidgetBlueprint.FigmaPrototypeSpecialActionsRuntime`、`Design2UMG.WidgetBlueprint.FigmaPrototypeHoverAndPressRuntime`、`Design2UMG.WidgetBlueprint.FigmaPrototypeSinglePageOpenLinkRuntime` 与 `Design2UMG.WidgetBlueprint.FigmaPrototypeSlideTransitionRuntime` 都覆盖了当前 prototype typed/runtime contract，因此表中显示 `[x]` 的能力已由测试链路验证。
>
> 注：Phase 2 下面的原始分解是历史方案，当前已经不再走 `BlueprintGraphHelper + K2 图表拼接` 这条实现路线，而是以 typed navigation contract + Blueprint extension 为主。

### 2.1 交互数据解析 🔴

| 任务 | 文件 | 状态 |
|------|------|------|
| FDesignNode 新增 `TArray<FPenInteraction> Interactions` | PenSchema.h | [ ] |
| 定义 `FPenInteraction`（Trigger + Action + TargetNodeId） | PenSchema.h | [ ] |
| 定义 `EPenTriggerType`（OnClick / OnHover / OnPress） | PenSchema.h | [ ] |
| 定义 `EPenActionType`（NavigateTo / SetVisibility / OpenURL） | PenSchema.h | [ ] |
| PenParser 解析 .pen 中的交互定义 | PenParser.cpp | [ ] |
| FigmaParser 解析 `transitionNodeID` + `interactions[]` | FigmaParser.cpp | [ ] |
| PenMCPBridge 传递交互数据 | PenMCPBridge.cpp | [ ] |

### 2.2 页面导航生成 🔴

**方案**：多个顶层 Frame → 自动包裹 UWidgetSwitcher；有导航交互的按钮 → 蓝图图表中添加 SetActiveWidgetIndex 节点。

| 任务 | 文件 | 状态 |
|------|------|------|
| Design2UMGConverter 检测多 Frame + 交互关系 | Design2UMGConverter.cpp | [ ] |
| 自动包裹 UWidgetSwitcher（多 Frame 场景） | Design2UMGConverter.cpp | [ ] |
| 创建 `BlueprintGraphHelper` 工具类 | BlueprintGraphHelper.h/cpp | [ ] |
| 实现 `AddOnClickedEvent()`（UK2Node_ComponentBoundEvent） | BlueprintGraphHelper.cpp | [ ] |
| 实现 `AddSetActiveWidgetIndex()`（UK2Node_CallFunction） | BlueprintGraphHelper.cpp | [ ] |
| 实现 Pin 查找与连接辅助函数 | BlueprintGraphHelper.cpp | [ ] |
| UMGGenerator 中调用蓝图图表生成 | UMGGenerator.cpp | [ ] |
| 编译蓝图并验证 | UMGGenerator.cpp | [ ] |

### 2.3 轻量替代方案（并行提供） 🟡

> 对于不想要蓝图图表自动生成的用户，提供 Tag 方案作为替代。

| 任务 | 文件 | 状态 |
|------|------|------|
| 有导航交互的 Widget 自动打 GameplayTag（`Nav.TargetFrame`） | UMGGenerator.cpp | [ ] |
| 提供辅助蓝图函数库 `UPenNavigationHelper` | PenNavigationHelper.h/cpp | [ ] |
| 实现 `SetupNavigation(UWidgetSwitcher*, UUserWidget*)` | PenNavigationHelper.cpp | [ ] |
| 设置项：选择生成模式（蓝图图表 / Tag / 无） | Design2UMGSettings.h | [ ] |

---

## Phase 3：开源后社区驱动（根据反馈排优先级）

> 以下功能在开源后根据 Issue/PR 热度决定优先级

### 3.1 高级交互 🟢

| 功能 | 说明 | 复杂度 |
|------|------|--------|
| Hover/Press 状态 | 解析 Variant 状态 → UButton Normal/Hovered/Pressed Style | 中 |
| 动画过渡 | 解析 transition 属性 → UWidgetAnimation | 高 |
| 条件显示 | 解析 conditional logic → SetVisibility 蓝图逻辑 | 中 |

### 3.2 响应式适配 🟢

| 功能 | 说明 | 复杂度 |
|------|------|--------|
| 断点系统 | 不同屏幕尺寸使用不同布局 | 高 |
| DPI 感知 | 材质/纹理在不同 DPI 下的表现 | 中 |
| Safe Zone | 移动端/主机安全区域适配 | 低 |

### 3.3 设计系统同步 🟢

| 功能 | 说明 | 复杂度 |
|------|------|--------|
| 设计令牌→UE5 DataAsset | 颜色/字体/间距令牌持久化 | 中 |
| 组件库映射 | 设计组件↔UE5 Widget 类双向绑定 | 高 |
| 增量更新 | 设计稿变更时只更新差异部分 | 高 |

---

## Open-Source Release Plan

### v1.0 release contents (current public boundary)

- Core conversion pipeline: `.pen`, Figma, and local Figma export JSON to UMG Widget Blueprint.
- Layout engine: Flexbox to Anchors/Offsets, plus Fixer rules.
- Current public visual-property path: fill, stroke, radius, shadow, and blur. The unreleased SDF material exploration is not a v1.0 promise.
- SVG path rasterization with caching.
- Component reference expansion plus variable / theme parsing.
- Content Browser drag-and-drop import and command-line import.
- Public smoke / validation documentation.
- Bridge and debug utilities remain available: MCP JSON bridge import and local Pencil MCP debug chain. These are not the current release support promise.
- Maintainer-owned formal admissible gate remains the release gate.

### v1.1 release contents (after Phase 2 expansion)

- Prototype flow expansion: hover, drag, keyboard, conditional flows, and overlay semantics.
- Evaluate whether automatic Blueprint graph generation should be added (`OnClicked -> SetActiveWidgetIndex`).
- Tag-based alternative option.
- Multi-stop gradients and arbitrary angles.

### v1.2+ (community driven)

- Advanced interaction, responsive adaptation, design-system sync, and similar follow-up areas.

---
## 关键技术决策记录

### 决策 1：CanContainContent 从 false 改为 true（历史探索，当前未采纳为 release 路线）

- **背景**：这是为 BoxShadow 裁剪问题探索过的一条材质化方向，不是当前仓库已经采纳的 release 决策
- **为什么没落地**：会丢失"纯代码零资产"特性，用户卸载插件后生成的 Widget 还会面临依赖管理问题，整体投入也偏高
- **当前结论**：保留为候选方案记录，不对外宣称已进入当前 release 主线

### 决策 2：Prototype Flow 主路线收口到 typed contract + Blueprint extension

- **背景**：早期确实评估过 `BlueprintGraphHelper + K2 图表拼接` 和 Tag 方案并行推进，但这已经不是当前主路线
- **原因**：UEdGraph authoring 维护成本高、跨版本稳定性差，继续把它写成主路线会和当前仓库现状冲突
- **当前结论**：公开主线以 typed navigation contract + Blueprint extension 为准；蓝图图表自动生成只保留为后续评估项，不再作为既定双轨结论写入 roadmap
- **参考**：Figma2UMG 的 WidgetBlueprintHelper（MIT）仍可作为后续评估实现时的参考

### 决策 3：椭圆走 Path 栅格化而非 SDF 材质

- **原因**：已有完整的 Path 指令集支持，椭圆→SVG path 转换简单，不需要额外材质资产
- **代价**：CPU 栅格化性能不如 GPU 材质，但椭圆使用频率低，可接受

---

## 验收标准

| 里程碑 | 完成标准 |
|--------|----------|
| **M0: 开源就绪** | `repository-hygiene` 公开检查通过、self-hosted build 可用、LICENSE/README/CONTRIBUTING 完善、版权头全部替换、无敏感信息 |
| **M1: v1.0 发布** | M0 达成，且当前公开 v1.0 内容收口完成；Path 缓存命中率 > 90%（通过 `Design2UMG.CacheStats` 验证）；不把已推迟的 BoxShadow/SDF、渐变材质路线写成 v1.0 完成条件 |
| **M2: v1.1 发布** | Phase 2 全部完成，Prototype Flow 端到端可用，蓝图编译无错误 |
| **M3: 社区健康度** | 3+ 外部 contributor、Issues 中位响应时间 < 48h |
