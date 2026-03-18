### #17 UMGJsonParser (1M 2L)

| 级别 | 问题 | 位置 |
|------|------|------|
| M | `ParseWidgetNode` 第148-173行的直接属性解析逻辑与 `ParseProperties` 函数（第375-413行）高度重复，但行为不一致：前者不处理 `EJson::Array` 类型，后者处理了。应提取为共用函数或直接委托给 `ParseProperties`，同时消除 Array 处理的不一致 | `ParseWidgetNode` vs `ParseProperties` |
| L | `ParseWidgetNode` 先调用 `ParseProperties` 写入 Properties map（第139行），再遍历直接属性用 `TMap::Add` 写入同一个 map（第155行）。同名 key 时直接属性静默覆盖 Properties 对象中的值，优先级语义隐含且无注释说明，维护者容易误解覆盖方向 | `ParseWidgetNode` 第135-173行 |
| L | `FUMGAnchorData::X/Y` 是 `float`，但 `ParseAnchorData` 中 `TryGetNumberField` 的输出参数是 `double`（`OutAnchor.X`/`OutAnchor.Y`），依赖 `double→float` 隐式窄化赋值。虽然 UE5 的 `TryGetNumberField(float&)` 重载可能存在，但当前代码实际匹配的是 `double` 重载，精度损失在 UI 坐标场景下无实际影响，但不够严谨 | `ParseAnchorData` 第358-359行 |
