### #10 Design2UMGConverter (2H 3M 1L)

| 级别 | 问题 | 位置 |
|------|------|------|
| H | `Convert` 通过 `TSharedPtr` 绕过 `const FDesignDocument&` 语义，静默修改调用者的原始数据：`Child->X/Y = 0`（L282-283）、`Node->Geometry = ...`（L362）、`PreComputeFitContentSizes` 大量回写 `Width.Value/Height.Value`。调用者无法预期输入被篡改，多次调用 `Convert` 会产生不同结果 | `Convert`, `PreComputeFitContentSizes`, `ConvertNode`(Ellipse) |
| H | `static TSet<FString> WarnedFamilies`（L752）非线程安全，同文件递归深度计数器用了 `thread_local`，说明存在多线程调用预期。`static` 局部 `TSet` 无锁并发读写会导致数据竞争 | `CreateIconFontWidget` |
| M | Ref 子节点在子节点循环中被手动展开（L549-563: `ExpandRef` + `ReThemeNode`），展开后传给 `ConvertNode`，而 `ConvertNode` 内部（L320-333）本身已有完整的 Ref 展开逻辑。虽然功能不出错（展开后类型非 Ref 不会二次触发），但这是冗余路径，`ExpandRef` 被多调用一次，`ReThemeNode` 逻辑重复 | `ConvertNode` 子节点循环 |
| M | `CreateIconFontWidget` 在转换过程中调用 `SaveConfig()`（L739）将推断的字体映射持久化到磁盘。转换器应是纯转换无副作用，持久化决策不应由此层负责（违反单一职责） | `CreateIconFontWidget` |
| M | 文本尺寸估算使用硬编码魔数 `1.5f`（行高系数）和 `0.6f`（字宽系数），对中文等宽字符偏差极大（中文实际约 1.0），且未提取为命名常量或可配置项，散布在 `EstimateNodeSize`（L166-168）和 `PreComputeFitContentSizes`（L909-912）两处 | `EstimateNodeSize`, `PreComputeFitContentSizes` |
| L | `Rectangle`（L349-351）与 `Frame/Group`（L345-348）处理完全相同，可合并为 fall-through 消除重复 case | `ConvertNode` switch |
