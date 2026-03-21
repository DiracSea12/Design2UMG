### #11 VisualMapper (2H 2M 2L)

| 级别 | 问题 | 位置 |
|------|------|------|
| H | Angular 渐变被当作 Radial 处理：`EPenGradientType::Angular` 直接 fallthrough 到 Radial 分支使用 `MatRef_Radial`，但角度渐变与径向渐变视觉完全不同，Angular 渐变渲染结果会不正确 | `ApplyFillProperties` L73-75, `WrapWithMultiFill` L491-493 |
| H | 多 Shadow 效果只保留最后一个：Effects 循环中多个 Shadow 用相同 key（如 `BoxShadowOffsetX`）调用 `Properties.Add`，后者覆盖前者，只有最后一个 Shadow 生效且无警告 | `ApplyVisualProperties` L166-199 |
| M | `BuildGradientMaterialParams` 只取首尾两色（`Colors[0]` 和 `Colors.Last()`），多色渐变（3+ color stop）的中间色全部丢弃，渐变效果与设计稿不一致 | `BuildGradientMaterialParams` L21-22 |
| M | `WrapWithBoxShadow` 中 Overlay Wrapper 接管了 Widget 的 Id/Name/Bounds/Slot，但未转移 Widget.Brush 数据，导致 DrawAs/OutlineCornerRadius 等视觉属性留在被降级为 `_content` 的内层节点上，Overlay 本身不渲染这些属性 | `WrapWithBoxShadow` L397-401 |
| L | 渐变方向判定逻辑（Linear→水平/垂直的 45° 阈值判断）在 `ApplyFillProperties` 和 `WrapWithMultiFill` 中完全重复，违反 DRY，修改阈值需同步两处 | `ApplyFillProperties` L80-84, `WrapWithMultiFill` L498-501 |
| L | `WrapWithBorder` 中属性迁移使用四处硬编码 key 列表（L282/L304/L324/L334），新增属性时必须在所有列表中同步添加，维护成本高且易遗漏 | `WrapWithBorder` L282-341 |
