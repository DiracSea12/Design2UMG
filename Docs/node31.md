### #31 UMGWidgetUtils (2M 3L)

| 级别 | 问题 | 位置 |
|------|------|------|
| M | `EffectiveDesignSize` 是可变全局静态变量，多处读写无同步机制。若未来有多线程或并发导入场景，会产生数据竞争。当前单线程可工作，但作为公共 API 暴露可变全局状态是不良设计，建议改为函数参数传递或封装为上下文对象 | `FUMGWidgetUtils::EffectiveDesignSize` (头文件 L32, cpp L62) |
| M | `HasValidSlotHistory` 将 Offsets 默认值硬编码为 `(0, 0, 100, 100)`，但 `FUMGSlotData` 的默认构造中 Offsets 默认值是全零 `FMargin(0)`。这意味着一个真正的"全默认"Slot（Offsets 全零）会被判定为"有历史数据"（因为 Right!=100, Bottom!=100），导致 Hysteresis 路径被错误触发 | `HasValidSlotHistory()` (cpp L509-521) |
| L | `ParseColor` 对 `#RGB` 短格式先展开为 `RRGGBB` 再调用 `FColor::FromHex`，但对 `#RRGGBB`/`#RRGGBBAA` 直接传入含 `#` 前缀的原始字符串 `Trimmed`。`FColor::FromHex` 能容忍 `#` 前缀所以功能正确，但两条路径处理风格不一致（一条传无 `#` 的 Hex，一条传带 `#` 的 Trimmed），阅读时容易误解 | `ParseColor()` (cpp L826-832) |
| L | `BuildAutoGridSlots` 排序 lambda 中用 `FMath::IsNearlyEqual` 比较 CenterY 来判断"同一行"，但排序比较函数要求严格弱序。`IsNearlyEqual` 的容差会导致 A==B 且 B==C 但 A!=C 的传递性违反，理论上可导致未定义排序结果。实际数据量小时不太会触发，但算法上不严谨 | `BuildAutoGridSlots()` 排序 lambda (cpp L269-276) |
| L | `CalculateDesignSize` 中 `TraverseNodeForDesignSize` 对非点锚点（拉伸锚点）的节点完全跳过尺寸计算，只递归子节点。如果根节点下直接有拉伸锚点的大控件，其实际占用面积不会被计入 MaxBounds，可能导致计算出的设计尺寸偏小 | `TraverseNodeForDesignSize()` (cpp L1033-1067) |
