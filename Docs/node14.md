### #14 LayoutFixer (1H 2M 2L)

| 级别 | 问题 | 位置 |
|------|------|------|
| H | `FixEqualSpacing` 边界检查硬编码 `0.0~1.0` 归一化范围，但 `FBounds` 在 pipeline 中并非总是归一化坐标。如果上游传入像素坐标的 Bounds，此检查会拒绝所有合法修正，等距功能静默失效 | `FixEqualSpacing` 第470/489行 |
| M | `FixChildrenSpacing` 中同级吸附用 `OriginalBounds[j]` 作为参考，但等距修正写入的是 `ProposedBounds`。吸附和等距两个 pass 之间存在数据不一致：吸附后的位置已写入 `ProposedBounds`，但等距检测仍基于 `OriginalBounds` 的间距判断，可能导致吸附结果被等距修正覆盖或冲突 | `FixChildrenSpacing` 第553~612行 |
| M | `DetectRows`/`DetectColumns` 只与组内第一个元素比较重叠度（`Bounds[Row[0]]`）。如果一行元素高度渐变（首元素最矮），后续较高元素可能因与首元素重叠不足而被错误分到新行，导致行检测碎片化 | `DetectRows` 第340行, `DetectColumns` 第380行 |
| L | `FixEqualSpacing` 计算了 `MedianGap` 和 `AvgGap` 两个值，但偏差判断用 `AvgGap`（`GetMaxDeviation(Gaps, AvgGap)`），实际修正却用 `MedianGap`。判断标准和修正目标不一致，边界情况下可能触发不必要的修正或漏掉应修正的情况 | `FixEqualSpacing` 第438~446行 |
| L | `GetMedian` 对输入数组做了完整拷贝+排序（`TArray<float> Sorted = InValues; Sorted.Sort()`），在 `FixChildrenSpacing` 的行/列循环中被间接调用。对于大量子节点的场景，重复拷贝排序有不必要的开销。可考虑就地排序或传入已排序数据 | `GetMedian` 第85行 |
