# PenToUMG Optimization Audit

Last updated: 2026-03-27

## 目标

本文件回答三个问题：

1. 当前插件在 `.pen -> UE5 UMG` 这条链路上，主架构是否正确
2. 还有哪些能力是可以做但还没做
3. 哪些能力属于 UMG 原生不直接支持，需要材质、RetainerBox 或自定义 Slate；以及哪些事情不值得作为主线做

本文不重新描述全部属性映射，只聚焦后续优化决策。

---

## 结论摘要

> 说明：
> - 本文中的“当前实现已具备”“源码已证实”等表述属于事实归纳
> - 本文中的“应做 / 不应做 / P0 / P1 / P2 / ROI”属于判断与建议
> - 如果把本文当严格审计报告使用，应继续把“事实”“判断”“建议”分开展示

### 1. 事实：主架构方向基本正确

当前实现已经站在一个合理的技术底座上：

- 先解析到统一 IR，再转换到 UMG，而不是边读边生成 Widget
- 以 Bounds 作为布局真值，在投影前用 `LayoutFixer` 修正
- 把解析、转换、修复、投影、生成拆成独立模块
- 对图片、路径、渐变、圆角、描边、变量、Ref 展开都已经有稳定主链

对于 `.pen -> 可维护 Widget Blueprint` 这个目标，这条路线是正确的。

### 2. 判断：当前还不是最优做法

离更强的版本，主要差在这几块：

- 原型/交互已完成高优先主线闭环，`hover/press/drag/keyboard/conditional` 已进入 typed runtime contract，但独立 overlay layer semantics 与完整 Smart Animate 仍未收口
- 高保真视觉效果主链已明显增强，但 full semantic parity 仍有缺口
- 属性系统仍有 legacy `PropertyBag` 包袱
- `ROADMAP` 历史分解区与现实现状仍有漂移

### 3. 判断：做不了的内容比想象中少

结合引擎源码判断，真正属于“UMG 完全做不了”的内容很少。大多数情况属于：

- UMG 原生能做，但插件还没接上
- UMG 原生没有现成控件，但可以通过 `RetainerBox + EffectMaterial`、材质 Brush、自定义 Slate 解决
- 理论能做，但不适合作为“可维护 UMG 树”主线目标

---

## 2026-03-27 对齐快照（替代旧结论）

> 本节用于替代本文中已经过期的“未实现”结论，统一给出当前状态，不夸大完成度。

### 已完成

- Figma `TRANSFORM_GROUP / TEXT_PATH / SLICE / FigJam-only` 节点策略已落地：
  - `TRANSFORM_GROUP`：进入运行时树的结构语义节点
  - `TEXT_PATH`：显式降级为普通文本并保留诊断语义
  - `SLICE` 与 FigJam-only 节点：显式跳过，不混入运行时树
- Figma prototype 高优先主链已落地：`ON_CLICK/ON_HOVER/ON_PRESS/ON_DRAG/ON_KEY_DOWN/ON_KEY_UP + NAVIGATE/SWAP/OVERLAY/BACK/OPEN_LINK`，以及 `SET_VARIABLE / SET_VARIABLE_MODE / CONDITIONAL` typed step program、`WidgetSwitcher` 收口、基础过渡近似和最小切页动画资产
- 前景 blur / inner shadow / background blur 已进入 typed runtime 主链（`RetainerBox + EffectMaterial` / `UBackgroundBlur`）
- 本地 Figma 导入链已覆盖高 ROI 的选择性 blend bake 子集（`LUMINOSITY/MULTIPLY/SCREEN/OVERLAY`）

### 部分完成

- Prototype 扩展能力仍未彻底收口：独立 overlay layer semantics、完整 Smart Animate、更广义 prototype state graph / animation authoring
- PropertyBag 强类型迁移仍是 typed-first + legacy fallback 双轨
- 视觉 fidelity 仍有尾项：`mesh_gradient`、`TEXT_PATH` 完整几何语义、full semantic blur/inner-shadow parity
- 验证体系已产品化，但结构断言粒度与长期 Designer parity 持续验证仍需补强

### 未完成

- 跨 `.pen` 与 Figma 的统一 prototype 语义导出仍未闭合；当前公开口径仍以 Figma prototype 为主
- legacy `Properties` 尚未退场，仍存在 class-specific 参数依赖

### 无法验证

- “是否已达到历史最佳真实视觉能力”与“Designer parity 最终效果”需要按当前基线协议重跑全量视觉回归后判定；仅靠静态代码审阅无法下结论

---

## 调研依据

### 仓库内实现

重点参考：

- `Source/Design2UMGCore/Private/PenParser.cpp`
- `Source/Design2UMGCore/Private/FigmaParser.cpp`
- `Source/Design2UMGCore/Private/Design2UMGConverter.cpp`
- `Source/Design2UMGCore/Private/VisualMapper.cpp`
- `Source/Design2UMGCore/Private/LayoutFixer.cpp`
- `Source/Design2UMGCore/Private/LayoutCalculator.cpp`
- `Source/Design2UMGEditor/Private/UMGGenerator.cpp`
- `Source/Design2UMGEditor/Private/UMGPropertyApplier.cpp`
- `Source/Design2UMGEditor/Private/UMGSlotApplier.cpp`

### UE5 引擎源码

重点核对了以下原生能力：

- `Engine/Source/Runtime/UMG/Public/Components/WidgetSwitcher.h`
- `Engine/Source/Runtime/UMG/Public/Animation/WidgetAnimation.h`
- `Engine/Source/Runtime/UMG/Public/Components/BackgroundBlur.h`
- `Engine/Source/Runtime/UMG/Public/Components/RetainerBox.h`
- `Engine/Source/Runtime/UMG/Public/Components/TextBlock.h`
- `Engine/Source/Runtime/UMG/Public/Components/TextWidgetTypes.h`
- `Engine/Source/Runtime/SlateCore/Public/Styling/SlateBrush.h`
- `Engine/Source/Runtime/SlateCore/Public/Brushes/SlateRoundedBoxBrush.h`

### 外部产品能力

Figma 官方帮助中心确认：

- 设计稿除了静态 UI，还可能带 `Prototype interactions / transitions / Smart Animate`
- 这类数据更像“状态切换 + 过渡参数”，不是 AE 那种完整时间线资产

---

## 官方参考文档

以下链接均为官方来源，后续做范围判断、补能力或修文档时应优先引用这些页面。

### Figma

- Developer docs 首页  
  <https://developers.figma.com/>
- REST API Files / Global properties  
  <https://developers.figma.com/docs/rest-api/files/>
- REST API File endpoints  
  <https://developers.figma.com/docs/rest-api/file-endpoints/>
- REST API File node types  
  <https://developers.figma.com/docs/rest-api/file-node-types/>
- REST API File property types  
  <https://developers.figma.com/docs/rest-api/file-property-types/>
- Plugin API node types  
  <https://developers.figma.com/docs/plugins/api/nodes/>
- Help Center 首页  
  <https://help.figma.com/>
- Create interactions  
  <https://help.figma.com/hc/en-us/articles/360040315773-Create-interactions>
- Prototype animations  
  <https://help.figma.com/hc/en-us/articles/360040522373-Prototype-animations>
- Smart animate  
  <https://help.figma.com/hc/en-us/articles/360039818874-Create-Advanced-Animations-with-Smart-Animate>
- Present designs and prototypes  
  <https://help.figma.com/hc/en-us/articles/360040318013-Present-designs-and-prototypes>

### Pencil / Pencil.dev

- Docs 首页  
  <https://docs.pencil.dev/>
- Installation  
  <https://docs.pencil.dev/getting-started/installation>
- AI Integration  
  <https://docs.pencil.dev/getting-started/ai-integration>
- Pencil Interface  
  <https://docs.pencil.dev/core-concepts/pencil-interface>
- `.pen` Files  
  <https://docs.pencil.dev/core-concepts/pen-files>
- The `.pen` Format  
  <https://docs.pencil.dev/for-developers/the-pen-format>
- Components  
  <https://docs.pencil.dev/core-concepts/components>
- Variables  
  <https://docs.pencil.dev/core-concepts/variables>
- Design as Code  
  <https://docs.pencil.dev/core-concepts/design-as-code>
- Design to Code  
  <https://docs.pencil.dev/design-and-code/design-to-code>
- Styles and UI Kits  
  <https://docs.pencil.dev/design-and-code/styles-and-ui-kits>
- Keyboard Shortcuts  
  <https://docs.pencil.dev/core-concepts/keyboard-shortcuts>

## 官方文档使用建议

### 1. 解析 / 数据结构

适用问题：

- 某个节点类型是不是官方支持
- 某个字段是不是标准字段
- 某种属性值是否属于合法枚举

优先看：

- Figma REST API File node types
- Figma REST API File property types
- Figma REST API Files / File endpoints
- Pencil `.pen` Files
- Pencil The `.pen` Format

### 2. 交互 / 动画 / Prototype

适用问题：

- 设计稿里会不会带交互
- Figma 的动画是什么级别的数据
- 插件应不应该支持导航、过渡、Smart Animate

优先看：

- Figma Create interactions
- Figma Prototype animations
- Figma Smart animate
- Figma Present designs and prototypes

### 3. 产品范围裁剪

适用问题：

- 当前该不该做 FigJam / Make / Slides / Buzz / Sites
- 哪些属于 Design 主线，哪些不该进入主线

优先看：

- Figma Help Center 首页
- Figma Developer docs 首页

### 4. 组件 / 变量 / 设计系统

适用问题：

- 组件实例、变量、设计系统映射怎么做
- `.pen` 与 UMG 设计令牌、主题系统如何对接

优先看：

- Pencil Components
- Pencil Variables
- Pencil Design as Code
- Pencil Design to Code
- Pencil Styles and UI Kits

### 5. 编辑器与工作流理解

适用问题：

- Pencil 的产品工作流如何组织
- 某个行为是 schema 问题还是编辑器行为问题

优先看：

- Pencil Interface
- Pencil Installation
- Pencil AI Integration

---

## 当前优势

### 1. 模块拆分是合理的

- `PenParser` 负责 schema / 变量 / descendants / 主题解析
- `Design2UMGConverter` 负责从设计语义到 Widget 树语义的转换
- `VisualMapper` 负责视觉属性包装和降级策略
- `LayoutFixer` 负责 Bounds 级修复
- `LayoutCalculator` 负责 Bounds -> Anchor / Offset 投影

### 2. 已经有正确的降级意识

当前实现并不是把所有非原生控件强行塞到单一 Widget 上，而是接受：

- Border 包裹
- Overlay 包裹
- 背景模糊包装
- 阴影包装
- path / 渐变 / 图形纹理化

### 3. 验证体系已经比较完整

`Docs/ValidationProtocol.md` 已经建立了：

- focused automation
- saved asset regeneration
- post-save PNG
- Designer screenshot
- structure snapshot

---

## Figma Design 节点覆盖边界

虽然本文主轴是 `.pen -> UMG`，但插件当前实际也承担 `Figma Design -> UMG`。因此需要把 Figma 剩余节点缺口明确写清楚。

当前 `FigmaParser::MapNodeType` 已覆盖主流 Design 节点，但仍有一批官方节点未显式映射。

### 1. 当前状态（2026-03-27）

| 节点类型 | 当前状态 | 备注 |
|---------|----------|------|
| `TRANSFORM_GROUP` | 已补齐 | 进入运行时结构语义 |
| `TEXT_PATH` | 已显式降级 | 当前是稳定 fallback，不做 text-on-path 几何复刻 |
| `SLICE` | 已显式跳过 | 保持设计辅助节点不进入运行时树 |

### 2. 不应纳入当前范围的 6 个

这 6 个本质上是 FigJam 侧节点，不该算在当前 `Figma Design -> UMG` 主线里：

- `TABLE`
- `TABLE_CELL`
- `STICKY`
- `SHAPE_WITH_TEXT`
- `CONNECTOR`
- `WASHI_TAPE`

### 3. 对 Figma 节点策略的收口结论

- 已补：`TRANSFORM_GROUP`
- 已显式降级：`TEXT_PATH`
- 已显式跳过：`SLICE`
- 继续排除在主线外：`TABLE`、`TABLE_CELL`、`STICKY`、`SHAPE_WITH_TEXT`、`CONNECTOR`、`WASHI_TAPE`

---

## 能力分类

## A. UMG 原生能做，但插件还没做完

| 能力 | 当前状态 | 引擎能力判断 | 建议 |
|------|----------|--------------|------|
| 页面切换 / 原型导航 | 已有最小运行时闭环 | `UWidgetSwitcher` 原生支持 | 扩容剩余交互语义 |
| Widget 动画 | 已有最小切页动画资产 | `UWidgetAnimation` 原生支持 | 扩展到更通用 authoring |
| 文本横向对齐 / 换行 / 行高 / 文本阴影 | 已部分支持 | `UTextBlock` / `UTextLayoutWidget` 原生支持 | 继续补齐 |
| 纯色圆角 / Outline | 已部分支持 | `RoundedBox` + `OutlineSettings` 原生支持 | 吃满原生能力 |
| Background blur | 已接通 | `UBackgroundBlur` 原生支持 | 继续完善 |
| 基础原型状态切换 | 已生成 | 可用事件 + WidgetSwitcher + 运行时扩展完成 | 继续扩容 |

### A1. Prototype Flow / Navigation 仍是最高 ROI 扩容项

原因：

- Figma / Pencil 原型里确实会有交互和过渡
- UMG 原生就有 `WidgetSwitcher`
- 当前仓库自己的 `ROADMAP` 也承认这部分是核心短板

当前已完成：

- 顶层多 Frame + 交互关系 -> 自动收口 `WidgetSwitcher`
- prototype 高优先闭环：`ON_CLICK/ON_HOVER/ON_PRESS/ON_DRAG/ON_KEY_DOWN/ON_KEY_UP + NAVIGATE/SWAP/OVERLAY/BACK/OPEN_LINK`
- `SET_VARIABLE / SET_VARIABLE_MODE / CONDITIONAL` 已进入 typed step program，并在 runtime contract 内执行

下一步建议：

- 把 overlay 从“只有切页语义”继续扩到更完整的浮层 placement / dismiss contract
- 继续补更完整动画与 Smart Animate 语义

优先级：`P0`

### A2. 动画不应追求全动画，但应做基础过渡

建议只做：

- Frame 切换淡入淡出
- 位移 / 透明度基础 transition
- 持续时间 / easing 映射

优先级：`P1`

### A3. 原生文本能力还没有完全吃满

引擎里原生已支持：

- `Justification`
- `AutoWrapText`
- `WrapTextAt`
- `LineHeightPercentage`
- `ShadowOffset`
- `ShadowColorAndOpacity`

但文本仍存在两个需要明确处理的点：

1. 垂直对齐不是 TextBlock 自身属性，需要通过容器或 Slot 做
2. 字体、换行、测量和设计稿仍会有误差，需要继续强化验证与 fallback

优先级：`P1`

---

## B. UMG 原生不直接支持，但插件完全可以补

| 能力 | 当前状态 | 是否值得做 | 备注 |
|------|----------|------------|------|
| 前景 blur / subtree blur | 已接入运行时近似 | 值得继续做 | 当前为可维护近似，不是 full semantic parity |
| Inner shadow | 已接入运行时近似 | 值得继续做 | 当前为单层近似，不是完整 inset-shadow 语义 |
| 多层高质量阴影 | 近似实现 | 值得 | 现状可继续增强 |
| 高保真 gradient | 已有 procedural 方案 | 值得 | 材质化后更稳 |
| Blend mode 的有限支持 | 大多缺失 | 选择性做 | 不追求通用图层混合 |
| 任意 shape/path 高保真 | 已栅格化 | 值得局部增强 | 不必改成矢量运行时控件 |

### B1. 应引入标准后处理包装层

现状里很多视觉问题本质相同：

- 前景 blur
- inner shadow
- 某些 blend / glow / mask 类效果

建议统一策略：

1. 识别节点是否需要 subtree effect
2. 外层包一层 `RetainerBox`
3. 给 `EffectMaterial`
4. 参数写入类型化字段

优先级：`P1`

### B2. 高保真 gradient 还值得继续做

当前代码已经有：

- linear / radial / angular 材料 key
- procedural texture 生成
- 多 stop 数据结构

说明方向已经具备，只是还没完全产品化。

优先级：`P1`

### B3. 阴影不要再停留在能看

现在的阴影实现比“完全不支持”好，但离稳定方案还有差距：

- text shadow 走原生
- 其他节点走包装近似
- inner shadow 已接入运行时近似，但仍不是完整语义复刻

优先级：`P1`

---

## C. 理论可做，但不该作为主线目标

| 能力 | 结论 | 原因 |
|------|------|------|
| `connection / note / prompt / context` 转运行时 UI | 不建议主线做 | 设计器语义，不是 runtime UI 语义 |
| 完整 blend graph 可编辑还原 | 不建议主线做 | 结果不可维护，容易演变成渲染系统 |
| text on path 原生可编辑支持 | 不建议主线做 | UMG 文本模型不匹配，ROI 低 |
| mesh gradient 完整可编辑控件化 | 不建议主线做 | 可硬做，但不适合作为 Widget 树主线 |
| FigJam / Make / Slides / Buzz / Sites 全支持 | 不建议主线做 | 偏离 `.pen / Design -> UMG` 核心价值 |

---

## 当前仓库最值得补的优化项

## P0

### 1. Prototype Flow 扩容（在已落地最小闭环基础上）

当前已完成：

- 顶层 Frame -> `WidgetSwitcher`
- `ON_CLICK/ON_HOVER/ON_PRESS/ON_DRAG/ON_KEY_DOWN/ON_KEY_UP + NAVIGATE/SWAP/OVERLAY/BACK/OPEN_LINK`
- `SET_VARIABLE / SET_VARIABLE_MODE / CONDITIONAL` typed step program
- 基础过渡近似 + 最小切页动画资产

后续目标：

- 独立 overlay layer semantics
- 更完整动画与 Smart Animate 语义

补充说明：

- 若主线继续包含 `Figma Design -> UMG`
- 那么 `Prototype Flow` 不能只改 `.pen` 路径
- 交互字段已进入 typed prototype contract，不再是“仅 metadata 保留”
- 仍需继续扩大 typed runtime contract 的覆盖面

### 2. PropertyBag 持续强类型化

现状：

- `FUMGWidgetNode` 仍保留 legacy `Properties`
- `UMGSchema.h` 已明确写了应完全重构为强类型数据的 TODO

建议：

- 新增 typed visual / text / effect / image / gradient 数据
- 新属性不再写入 `Properties`
- `Properties` 仅保留兼容层
- 逐步把 `UMGPropertyApplier`、`UMGJsonParser`、`UMGJsonSerializer` 迁移到 typed path

### 3. 文档与实际实现对齐

当前文档已有明显漂移，需要单独治理。

### 3.1 当前仍需收口的文档漂移项（2026-03-27）

| 文档位置 | 当前问题 | 处理建议 |
|---------|----------|---------|
| `Docs/ROADMAP.md` | 历史分解区仍保留“prototype 未实现 / 蓝图图表生成待做”等旧路线描述 | 保留历史分解，但加“历史方案”标记并以 2.0 状态为准 |
| 本文（0324 审计） | 多处“未实现”结论已过期 | 用“2026-03-27 对齐快照”覆盖并替换旧结论 |

建议：

- `PropertyMapping.md`
- `Design2UMG_TechnicalDesign.md`
- `KNOWN_LIMITATIONS.md`

作为一组一起同步，而不是零散改一句话。

## P1

### 4. 统一后处理包装层

目的：

- 接管 foreground blur
- inner shadow
- 某些局部 blend / glow 效果

### 5. 视觉保真增强

重点：

- gradient fidelity
- shadow fidelity
- stroke align / per-side stroke 稳定性
- text measurement / wrap parity
- path 持久化与缩放边缘 case

### 6. 验证体系继续产品化

建议继续补：

- 视觉对比专门 fixture
- effect wrapper 专门 fixture
- interaction/navigation 端到端自动化
- asset save 后结构断言更强

## P2

### 7. 选择性 blend mode 支持

建议只做高频子集：

- multiply
- screen
- overlay

### 8. 设计器专用节点保留策略

现状是直接 skip。

可改进为：

- 仍不生成 runtime widget
- 但写入诊断报告 / metadata / conversion warnings
- 允许导出设计注释清单

---

## 明确的范围建议

## 应该坚定坚持的主线

- `.pen -> UMG`
- `Figma Design -> UMG`
- 目标是可维护 Widget Blueprint

在 `Figma Design -> UMG` 这条线上，建议额外明确：

- 补齐 `TRANSFORM_GROUP`
- 对 `TEXT_PATH` 做显式 fallback
- 对 `SLICE` 做显式跳过
- 不把 `TABLE / TABLE_CELL / STICKY / SHAPE_WITH_TEXT / CONNECTOR / WASHI_TAPE` 纳入当前主线

## 不应该成为主线的方向

- Figma 全家桶产品全支持
- 完整设计器协作语义运行时化
- 过度追求图层级 blend fidelity
- 为了少数效果引入整套复杂渲染系统

---

## 推荐路线图

## 阶段 1：把可交付补齐

目标：

- Prototype navigation
- typed property migration
- 文档同步

## 阶段 2：把视觉保真补强

目标：

- RetainerBox effect pipeline
- gradient / shadow / blur 提升
- 文本与 Designer parity 提升

## 阶段 3：把产品体验补稳

目标：

- 警告体系
- 导入报告
- fixture 扩充
- 更清晰的 unsupported / degraded reason 输出

---

## 最终判断

### 事实 + 判断：当前插件是不是正确路线

是。

### 判断：当前插件是不是已经是最优实现

不是。

### 建议：现在最该补什么

按 ROI 排序：

1. Prototype Flow / Navigation
2. 强类型属性系统
3. 文档与实现同步
4. RetainerBox / 材质后处理能力
5. 视觉保真增强

### 建议：现在最不该做什么

1. 把设计器语义节点强转成运行时 UI
2. 追求 Figma 全产品全支持
3. 在主线里做全量 blend graph 还原
4. 为少数边缘视觉效果重写一套 UI 渲染器

---

## 附：文档与测试收口状态（2026-03-27）

### 文档状态

- `Docs/PrototypeFlowPlan.md`
- `Docs/EffectPipelinePlan.md`
- `Docs/PropertySystemMigrationPlan.md`
- `Docs/DocumentationDriftFixPlan.md`

以上文档均已落地，2026-03-27 快照后新增内容都应以此为基线，并配合下文列出的自动化测试信号确认状态。

### 测试状态与待补项

- Prototype 自动化闭环：`Design2UMG.Converter.FigmaPrototypeNavigation`、`Design2UMG.Converter.FigmaPrototypeSpecialActions`、`Design2UMG.Converter.FigmaPrototypeHoverAndPress`、`Design2UMG.Converter.FigmaPrototypeKeyboardAndConditional`、`Design2UMG.WidgetBlueprint.FigmaPrototypeNavigationRuntime`、`Design2UMG.WidgetBlueprint.FigmaPrototypeSpecialActionsRuntime`、`Design2UMG.WidgetBlueprint.FigmaPrototypeHoverAndPressRuntime`、`Design2UMG.WidgetBlueprint.FigmaPrototypeAdvancedTriggersRuntime`、`Design2UMG.WidgetBlueprint.FigmaPrototypeSinglePageOpenLinkRuntime` 和 `Design2UMG.WidgetBlueprint.FigmaPrototypeSlideTransitionRuntime` 都验证了当前 prototype contract 的切页、过渡、非 click trigger 与 runtime binding 主链。
- Effect / gradient 相关：`Design2UMG.Converter.ForegroundBlurRetainerWrapper`、`Design2UMG.Shadow.InnerShadowRetainerWrapper` 与 `Design2UMG.BatchImport.FigmaLocalLayerBlurBake` 关注 RetainerBox 包裹、blur wrapper 与 grad bake。
- Typed property / effect schema：`Design2UMG.UMGJson.TypedBrushImageGradientRoundTrip`、`Design2UMG.VisualMapper.WrapWithBorderDirect`、`Design2UMG.VisualMapper.WrapWithBorderTyped`、`Design2UMG.UMGJson.InnerShadowEffectRoundTrip` 与 `Design2UMG.FigmaParser.EffectsConvertToTypedWrappers` 确认 typed 主链与当前 widget-specific property bag 的 round-trip。
- 仍建议继续补强：
  - overlay 独立层语义相关断言
  - 长期 Designer parity 的批量基线复验
