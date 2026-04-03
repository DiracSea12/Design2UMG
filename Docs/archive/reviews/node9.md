### #9 PenTypeMapper (1M 2L)

| 级别 | 问题 | 位置 |
|------|------|------|
| M | `MapNodeTypeToWidget` 未处理 `EPenNodeType::Ref`。虽然调用方（Design2UMGConverter）在调用前已过滤 Ref 节点，但映射函数本身对 14 个枚举值中的 5 个（Ref, Connection, Note, Prompt, Context）静默落入 default 返回 CanvasPanel。其中 Connection/Note/Prompt/Context 是设计器专用类型（已被 `IsDesignerOnlyNodeType` 过滤），但 Ref 不在该过滤列表中，属于语义遗漏。如果未来调用路径变化，Ref 节点会被错误映射为 CanvasPanel 而非产生编译期或运行期警告 | `MapNodeTypeToWidget` switch 的 default 分支 |
| L | `MapFontWeight` 用 if-else 链做字符串匹配，18 次比较。当前规模可接受，但若后续扩展字重（如 CSS `950` ultra-black），维护成本线性增长。可考虑用 `TMap<FString, FName>` 静态表替代 | `MapFontWeight` |
| L | `Text` 和 `IconFont` 两个 case 返回相同的 `"TextBlock"`，可合并为 fall-through 写法（类似 Rectangle/Ellipse/Polygon/Path 的处理方式），减少重复 | `MapNodeTypeToWidget` 第 22-27 行 |
