### #28 IncrementalUpdater (1H 2M 1L)

| 级别 | 问题 | 位置 |
|------|------|------|
| H | `FilterByAccepted` 无法正确处理被拒绝的 `Removed` 条目。当用户取消勾选一个 Removed entry（语义="我不想删除这个旧节点"），该 NodeId 被加入 `RejectedIds`，但过滤逻辑是从**新树**中移除匹配节点——而 Removed 节点本就不存在于新树中，所以拒绝操作实际上是空操作。要正确实现"保留旧节点"，需要将旧 Widget 合并回新树，当前纯过滤架构做不到这一点 | `FilterByAccepted` |
| M | `IsNodeModified` 只比较了 Widget 类型和名称，未比较任何视觉属性（Bounds、Properties、Brush、TextStyle 等）。属性级别的变更不会被检测为 Modified，用户在 diff 预览中看不到这些变化，导致增量更新可能静默覆盖手动调整过的属性 | `IsNodeModified` |
| M | `CollectExistingIds` 和 `CollectNewIds` 使用 `TMap::Add`，当存在重复 NodeId 时静默覆盖旧值。如果 WidgetTree 中因 bug 或手动编辑出现重复 ID，先遍历到的 Widget 会被丢弃，diff 结果不可预测 | `CollectExistingIds` / `CollectNewIds` |
| L | `CountAdded`、`CountModified`、`CountRemoved` 三个函数各自独立遍历整个 Entries 数组。在 `ShowDiffPreviewDialog` 中三者连续调用，产生 3 次 O(n) 遍历。可合并为单次遍历返回三个计数，虽然 n 通常不大，但属于不必要的重复工作 | `FDiffResult::CountAdded/Modified/Removed` |
