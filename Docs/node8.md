### #8 RefExpander (2H 2M 1L)

| 级别 | 问题 | 位置 |
|------|------|------|
| H | `DeepCopyNode` 用 `*Copy = *Source` 浅拷贝后只深拷贝了 `Children`，遗漏了 `Descendants`（`TMap<FString, TSharedPtr<FDesignNode>>`）。拷贝结果与源节点共享 Descendants 内的指针，后续 `MergeDescendants` / `ApplyOverrideProperties` 修改时会污染源数据 | `DeepCopyNode` L232-239 |
| H | `ApplyDescendantOverride` 对象替换模式直接 `Node->Children[i] = Override` 将外部指针插入树中，未做 DeepCopy。同一 Override 被多个实例引用时，修改一个实例会连带影响其他实例 | `ApplyDescendantOverride` L276 |
| M | `ApplyOverrideProperties` 和 `ApplyInstanceOverrides` 中 `Target.Children = Override.Children` / `Expanded->Children = RefNode->Children` 是 TSharedPtr 数组的浅拷贝，Target 与源共享子节点指针，后续对子树的修改会交叉污染 | `ApplyOverrideProperties` L72, `ApplyInstanceOverrides` L173 |
| M | `ApplyOverrideProperties` 对 `JustifyContent`/`AlignItems`/`TextAlign` 用枚举默认值（`Start`/`Left`）判断"未设置"，导致无法通过 Override 将属性显式重置回默认值。其他属性已用 `bHasX` 守卫模式解决，这三个不一致 | `ApplyOverrideProperties` L61-66 |
| L | `ApplyOverrideProperties` 与 `ApplyInstanceOverrides` 覆盖的属性集不一致——前者处理 `Content`/`FontSize`/`FontWeight`/`FontFamily`/`LineHeight`/`TextGrowth`/`TextAlign` 等文本属性，后者处理 `Rotation`/`bFlipX`/`bFlipY`/`bHasWidth`/`bHasHeight`；两个函数各自覆盖了对方没有的属性，语义边界不清晰，维护时容易遗漏 | `ApplyOverrideProperties` vs `ApplyInstanceOverrides` |
