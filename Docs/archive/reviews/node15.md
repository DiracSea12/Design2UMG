### #15 LayoutValidator (1H 1M 1L)

| 级别 | 问题 | 位置 |
|------|------|------|
| H | FBounds 定义为像素坐标（注释 `左上角X（像素）`），但 `CalculateBoundsError` / `CalculatePosError` 将其当归一化值（0~1）使用，乘以 DesignSize 转像素。如果调用方传入的是像素 FBounds，误差会被放大 1920x 或 1080x 倍；如果传入的确实是归一化值，则与 FBounds 的类型定义和注释矛盾，属于类型滥用。无论哪种情况，接口契约不清晰，集成时极易出 bug | `CalculateBoundsError` / `CalculatePosError` 第11-14行、第27-28行 |
| M | `CalculatePosError` 已定义并实现，但整个插件中无任何调用点（包括 CalculateMetrics 内部也只用了 CalculateBoundsError）。属于死代码，增加维护负担 | `CalculatePosError` |
| L | 命名空间 `EvalThresholds` 不符合 UE5 命名惯例（UE5 习惯用 `namespace Design2UMG` 或类内 static constexpr），且 `Acceptance_MAE` 的下划线混合大驼峰风格与 UE5 编码规范不一致 | `EvalThresholds` 命名空间，第9-13行 |
