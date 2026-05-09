# Design2UMG 属性类型化契约与维护说明

本文记录当前代码里的属性类型化契约，不是改造 proposal。

## 当前主路径

Design2UMG 的导入与生成链路现在按下面的职责分层：

```text
.pen / Figma export
  -> parser / mapper
  -> FUMGWidgetNode typed payloads
  -> Design2UMGWidgetNodeAccess::BuildTypedPropertyBag
  -> UMG property appliers / resource binders
```

`FUMGWidgetNode` 的主属性不是通用字符串 map。节点上的属性按领域放在强类型 payload 中，例如：

| 领域 | 当前 payload |
|------|--------------|
| 文本样式 | `FUMGTextStyleData` |
| 控件样式 | `FUMGWidgetStyleData` |
| 文本内容与行为 | `FUMGTextData` |
| 视觉画刷、图片、渐变 | `FUMGVisualBrushData` |
| 效果 | `FUMGEffectData` |
| 路径与向量 | `FUMGPathData` |
| 阴影 | `FUMGShadowData` |
| 渲染样式 | `FUMGRenderStyleData` |
| 控件状态 | `FUMGWidgetStateData` |
| ScrollBox 配置 | `FUMGScrollBoxData` |

最终 UMG 侧仍需要一个按 key 读取的桥接对象，这个对象是 `FUMGPropertyBag`。它不是节点上的持久旧状态，而是在生成阶段由 typed node 数据重建出来。

## 关键边界

### `FUMGWidgetNode`

`FUMGWidgetNode` 是导入后的中间节点。它保留布局、slot、children、source/prototype 扩展数据，并聚合各类 typed payload。

维护规则：

- 新的节点属性优先加入对应 typed payload。
- 不要在 `FUMGWidgetNode` 上重新增加通用字符串属性表。
- 不要为同一属性做 typed payload 与字符串属性表的双写。
- source/prototype 相关数据继续走显式 opt-in 扩展，不放回默认聚合面。

### `FUMGPropertyBag`

`FUMGPropertyBag` 是最终消费者桥。它负责把 typed 值以稳定 key 暴露给 UMG property applier、resource binder 和少量生成期工具。

它的定位是“生成期消费接口”，不是 parser 的真实数据模型。维护时要保持这个方向：

- parser / mapper 写 typed payload。
- Core 中的 `Design2UMGWidgetNodeAccess` 负责把 typed payload 序列化成 property bag。
- Editor 中的 UMG applier 从 `FUMGPropertyBag` 读取最终可应用值。
- 如果新属性只服务某个 typed domain，先补 typed payload 和 `Design2UMGWidgetNodeAccess`，再让 applier 读取 bag。

### `Design2UMGWidgetNodeAccess`

`Design2UMGWidgetNodeAccess` 是跨模块读取 typed node 的稳定门面。Core 拥有 typed payload 与序列化规则，Editor 不应该绕过它去依赖 Core 私有实现。

常用入口：

| 入口 | 用途 |
|------|------|
| `BuildTypedPropertyBag(const FUMGWidgetNode&)` | 为最终 UMG 消费者构建完整属性包 |
| `TryGetWidget*` | 以类型化方式读取单个 widget key |
| `TryGet*TypedWidgetString` | 领域内字符串序列化读取，主要用于测试和跨模块契约 |
| `SerializeWidget*` | 统一基础类型序列化 |

## 渐变与材质参数

渐变已经有当前 typed contract：

- typed 数据结构：`FUMGVisualBrushData::FGradientData`
- material ref 派生：`Design2UMGWidgetNodeAccess::GetGradientMaterialRef`
- material params 序列化：`Design2UMGWidgetNodeAccess::SerializeGradientMaterialParams`

因此维护渐变时不要再把 `MaterialParams` 当成待拆分的开放项。主路径是：parser / mapper 写入 `Brush.Gradient`，Core 序列化成最终 UMG 消费所需的 material ref 与 params。

## 新增属性流程

新增一个属性时，按这个顺序做：

1. 判断它属于哪个 typed domain。
2. 在对应 payload 里增加字段，并明确 unset 与显式值的语义。
3. 在 parser / mapper 写入 typed 字段。
4. 在 `Design2UMGWidgetNodeAccess` 中补读取和序列化。
5. 在对应 UMG applier / binder 中通过 `FUMGPropertyBag` 消费。
6. 增加覆盖 typed payload、property bag 和最终 UMG 应用结果的测试。

如果属性语义只是最终 UMG 应用细节，且源数据没有独立领域含义，也可以只在 property bag 消费层处理；但不能因此把 parser 主数据模型退回通用字符串 map。

## 不要做什么

| 做法 | 原因 |
|------|------|
| 重新增加名为 `Properties` 的通用字符串属性表 | 会把主数据模型拉回弱类型通道 |
| typed payload 和字符串 map 双写 | 会制造两个事实来源 |
| 让 Editor 直接读取 Core 私有实现 | 会破坏跨模块边界 |
| 用单个宽泛 bag 代替 typed payload | 会丢掉领域语义和 unset 语义 |
| 为单个样例或单个设计稿定制 key / 分支 | 会变成过拟合，不符合正式 gate 要求 |

## 相关代码入口

| 文件 | 说明 |
|------|------|
| `Source/Design2UMGCore/Public/UMGSchema.h` | `FUMGWidgetNode` 聚合 typed payload |
| `Source/Design2UMGCore/Public/UMGSchemaTypedPayloads.h` | typed payload 定义 |
| `Source/Design2UMGCore/Public/UMGSchemaFoundation.h` | `FUMGTypedProperty` 与 `FUMGPropertyBag` |
| `Source/Design2UMGCore/Public/UMGWidgetNodeAccess.h` | typed node 跨模块读取契约 |
| `Source/Design2UMGCore/Private/UMGWidgetNodeAccess.cpp` | property bag 构建与序列化规则 |
| `Source/Design2UMGEditor/Private/UMGPropertyApplierInternal.cpp` | widget property 应用注册表 |
| `Source/Design2UMGEditor/Private/UMGBrushPropertyApplier.cpp` | brush / image / border 等视觉属性应用 |
| `Source/Design2UMGEditor/Private/UMGTextPropertyApplier.cpp` | 文本属性应用 |
