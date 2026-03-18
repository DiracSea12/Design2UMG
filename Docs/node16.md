### #16 UMGGenerator (1M 2L)

| 级别 | 问题 | 位置 |
|------|------|------|
| M | `EffectiveDesignSize` 是 static 全局变量，在 `BuildWidgetBlueprint` 中直接赋值（第98行）。若未来多线程或批量生成多个 Widget Blueprint，后一次调用会覆盖前一次的值，导致并发场景下布局计算使用错误的 DesignSize。应改为通过参数传递而非依赖全局状态 | `BuildWidgetBlueprint` 第98行 `FUMGWidgetUtils::EffectiveDesignSize = DesignSize` |
| L | Verbose 模式下的 `DumpNode` lambda（第104-131行）构建缩进字符串使用循环拼接 `+=`，深层节点会产生大量临时 FString 分配。可改用 `FString::ChrN(Depth * 2, TEXT(' '))` 一次分配 | `BuildWidgetBlueprint` 第106-107行 DumpNode lambda |
| L | 冲突重命名循环（第42-46行）的 `Suffix` 从1开始，循环条件 `Suffix <= MaxAttempts` 意味着实际最多尝试1000次后 Suffix 变为1001，但第48行检查 `Suffix > MaxAttempts` 时 Suffix 已经是最后一次 `++Suffix` 后的值。逻辑正确但不直观——循环退出时 Suffix 可能是1001（超限退出）或中间值（找到可用名），建议用 `bFound` 标志使意图更清晰 | `BuildWidgetBlueprint` 第38-53行冲突重命名逻辑 |
