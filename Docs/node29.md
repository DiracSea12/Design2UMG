### #29 ProceduralShapeGenerator (2M 4L)

| 级别 | 问题 | 位置 |
|------|------|------|
| M | `Outer` 参数是死代码。`CreateTransientTexture` 接收 `Outer` 但从未使用——`UTexture2D::CreateTransient` 的签名不接受 Outer 参数。所有 Generate* 函数的 `Outer` 参数层层传递最终无效，造成 API 误导：调用方以为传入的 Outer 会控制纹理生命周期，实际并没有 | `CreateTransientTexture`、所有 `Generate*` 函数签名 |
| M | 头文件注释声称"无需显式清理 - Unreal GC 自动处理销毁"，但实现中 `AddToRoot()` 会阻止 GC 回收，必须手动调用 `ReleaseTransientTextures()` 才能释放。注释与实现矛盾，容易误导使用者泄漏纹理 | `.h` 第 16-18 行注释 vs `.cpp` `CreateTransientTexture` 第 30 行 `AddToRoot()` |
| L | `GenerateFromMaterialRef` 中 `ColorStart`/`ColorEnd` 变量同时承载线性渐变和径向渐变的颜色。径向渐变语义是 Center/Edge，但传给 `GenerateRadialGradient` 的实参名为 `ColorStart`/`ColorEnd`，语义不匹配。虽然值正确（通过 `ColorCenter`/`ColorEdge` JSON key 解析），但可读性差，后续维护容易混淆 | `GenerateFromMaterialRef` 第 258-259 行、第 305 行 |
| L | `GenerateRing` 的 `InnerRadius = Center * (1.0f - Thickness * 2.0f)`，当 `Thickness >= 0.5` 时内半径 <= 0，环退化为实心圆。`Thickness` 的有效范围 `(0, 0.5)` 未在注释或 Clamp 中体现，且 `GenerateFromMaterialRef` 的归一化逻辑允许传入任意值 | `GenerateRing` 第 95 行 |
| L | `GenerateRoundedRect` 四角圆心计算不对称：右下/右上/左下角用了 `Width - Radius - 1` 和 `Height - Radius - 1` 偏移，但左上角圆心是 `(Radius, Radius)` 无偏移。在小尺寸纹理上可能导致右侧/底部圆角比左上角略"瘦"一个像素 | `GenerateRoundedRect` 第 146-163 行 |
| L-性能 | `FillTextureData` 每次调用都执行 `FlushRenderingCommands()`，这是一个阻塞游戏线程等待 GPU 的重量级同步。批量生成多个形状时（如一次导入多个节点），会反复阻塞。可考虑只在批量结束后 flush 一次 | `FillTextureData` 第 51 行 |
