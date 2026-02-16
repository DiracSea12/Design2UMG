# Design2UMG 开发路线图

**创建日期**：2026-02-15
**基于**：代码 Review + 竞品调研 + 三轮交叉验证
**状态说明**：`[ ]` 待开始 | `[~]` 进行中 | `[x]` 已完成

---

## 战略定位：开源核心 + 商业增值

### 为什么开源

- UE5 生态内"设计→UMG"赛道几乎空白，仅有 Figma2UMG（29 stars，从付费转免费）和 Figma2Unreal（87 stars，自称 rudimentary）
- Unity 生态的 Figma Converter for Unity（$99，49 评价，6 年持续更新）验证了市场需求存在，但 UE 侧付费意愿不足
- Design2UMG 的架构完成度远超现有竞品，开源可快速建立生态位
- AI 设计转代码浪潮尚未进入游戏引擎领域，时间窗口有限

### 商业增值方向（开源后探索）

- Pencil MCP 实时同步作为 Pro 功能
- 企业级支持/咨询
- 高级功能插件（动画状态导出、设计系统同步）

---

## 竞品差距分析

| 能力 | Design2UMG | Figma2UMG | 差距 |
|------|----------|-----------|------|
| 布局转换 | ✅ Flexbox→Anchors，9宫格+Hysteresis | ✅ 基础布局 | 领先 |
| 视觉属性 | ✅ 填充/描边/圆角/阴影/模糊 | ⚠️ 基础填充 | 领先 |
| 组件系统 | ✅ Ref展开+变量解析+主题 | ✅ Components/Variants | 持平 |
| Prototype Flow | ❌ 无 | ✅ 交互解析+蓝图图表生成 | **落后** |
| 渐变 | ⚠️ 双色H/V/Radial | ❌ 无 | 领先 |
| SVG Path | ✅ 完整指令集栅格化 | ❌ 无 | 领先 |
| 多输入源 | ✅ .pen + Figma + MCP | Figma only | 领先 |
| 蓝图逻辑生成 | ❌ 仅Widget树 | ✅ UEdGraph节点 | **落后** |

**结论**：核心短板是 Prototype Flow 和蓝图逻辑生成。Figma2UMG 的 `WidgetBlueprintHelper`（MIT）可作为参考实现。

---

## Phase 0：开源准备

> 目标：代码清理、文档完善，达到开源发布标准

| 任务 | 文件 | 状态 | 说明 |
|------|------|------|------|
| **替换版权头**（阻塞项） | 全部 .h/.cpp | [x] | `Diracsea12` MIT 版权头 |
| 清理内部引用和硬编码路径 | 全局 | [x] | 扫描完成，无敏感信息 |
| 补充 LICENSE 文件 | LICENSE | [x] | MIT |
| 完善 README（英文为主，中文可选） | README.md | [x] | 多输入源/架构图/Quick Start/Known Limitations |
| 补充 CONTRIBUTING.md | CONTRIBUTING.md | [x] | 贡献指南、代码规范、架构概览 |
| 创建 GitHub Actions CI | .github/workflows/ | [x] | 编译检查模板（UE 5.4 / 5.5 / 5.6 / 5.7），需配置 self-hosted runner |
| 创建 GitHub Release 模板 | .github/ | [x] | PR 模板 + Bug Report Issue 模板 |
| 整理 CHANGELOG | CHANGELOG.md | [x] | Keep a Changelog 格式，v1.0.0 + Unreleased |
| 更新 .uplugin 元信息 | Design2UMG.uplugin | [x] | CreatedBy / DocsURL / SupportURL |

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

### 1.2 BoxShadow SDF 材质替换 ⏸️ 推迟

**现状**：Overlay + 高斯模糊 Image 方案，ClipToBounds 下阴影被裁剪，只支持单层外阴影+均匀圆角。CPU 方案无法根治。

**推迟原因**：性价比低。阴影裁剪只在紧凑布局+父级 ClipToBounds 时出现，属于边缘 case。引入材质资产会丢失"纯代码零资产"卖点，9 个子任务工程量大。推迟到社区有需求时再做。

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

### 2.1 交互数据解析 🔴

| 任务 | 文件 | 状态 |
|------|------|------|
| FPenNode 新增 `TArray<FPenInteraction> Interactions` | PenSchema.h | [ ] |
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

## 开源发布计划

### v1.0 发布内容（Phase 0 + Phase 1 完成后）

- 核心转换管线（.pen / Figma / MCP → UMG Widget Blueprint）
- 布局引擎（Flexbox → Anchors/Offsets，Fixer 规则修复）
- 视觉属性（填充/描边/圆角/阴影/模糊，SDF 材质方案）
- SVG Path 栅格化（带缓存）
- 组件引用展开 + 变量/主题解析
- Content Browser 拖拽导入 + Commandlet 批量导入
- Pencil MCP 实时连接
- 自动化测试

### v1.1 发布内容（Phase 2 完成后）

- Prototype Flow（页面导航逻辑生成）
- 蓝图图表自动生成（OnClicked → SetActiveWidgetIndex）
- Tag 方案替代选项
- 渐变多色站点 + 任意角度

### v1.2+（社区驱动）

- 高级交互、响应式适配、设计系统同步等

---

## 关键技术决策记录

### 决策 1：CanContainContent 从 false 改为 true

- **原因**：BoxShadow 的 ClipToBounds 裁剪问题用 CPU 方案无法解决，必须用 SDF 材质
- **代价**：丢失"纯代码零资产"特性，用户卸载插件后生成的 Widget 会有 broken reference
- **缓解 A**：材质资产最小化（仅 BoxShadow + RoundedRect + Gradient），文档中说明依赖关系
- **缓解 B**：生成时将 MID 渲染结果烘焙为独立 UTexture2D 序列化进 Widget 资产（MID → RenderTarget → ReadBack → UTexture2D → SavePackage），已生成的 Widget 脱离插件依赖，卸载后不 broken

### 决策 2：蓝图图表生成采用双轨方案

- **原因**：UEdGraph API 无官方文档，维护成本高，UE 版本升级可能 break
- **方案**：同时提供蓝图图表生成（功能完整）和 Tag 方案（轻量稳定），用户可选
- **参考**：Figma2UMG 的 WidgetBlueprintHelper（MIT）已验证可行性

### 决策 3：椭圆走 Path 栅格化而非 SDF 材质

- **原因**：已有完整的 Path 指令集支持，椭圆→SVG path 转换简单，不需要额外材质资产
- **代价**：CPU 栅格化性能不如 GPU 材质，但椭圆使用频率低，可接受

---

## 验收标准

| 里程碑 | 完成标准 |
|--------|----------|
| **M0: 开源就绪** | CI 通过、LICENSE/README/CONTRIBUTING 完善、版权头全部替换、无敏感信息 |
| **M1: v1.0 发布** | Phase 1 全部完成，Path 缓存命中率 > 90%（通过 `Design2UMG.CacheStats` 验证），BoxShadow 不再被裁剪 |
| **M2: v1.1 发布** | Phase 2 全部完成，Prototype Flow 端到端可用，蓝图编译无错误 |
| **M3: 社区健康度** | 3+ 外部 contributor、Issues 中位响应时间 < 48h |
