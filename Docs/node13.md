### #13 LayoutCalculator (1M 2L)

| 级别 | 问题 | 位置 |
|------|------|------|
| M | `CalculateAxis` 中 `TOptional<float> OldAnchor` 和 `const FLayoutPreset& Preset` 按值传递了 `TOptional`，每次调用产生不必要的拷贝；应改为 `const TOptional<float>&` | `CalculateAxis` 签名 |
| L | `SelectAnchorWithHysteresis` 的 `Old == 1.0f` 分支仅用 `// Old == 1.0f` 注释，前两个分支用了 `IsNearlyEqual` 做浮点比较，第三个分支隐式依赖 else 兜底，风格不一致且如果未来新增锚点值（如 0.25）会静默落入此分支 | `SelectAnchorWithHysteresis` 第35行 else 分支 |
| L | `bEnableNineGrid = false` 时 Anchor 硬编码为 `0.0f`（所有 widget 锚定左上角），缺少注释说明这是有意的 fallback 行为还是遗漏；调用方如果不知道这个隐含语义，可能误以为是 bug | `CalculateAxis` 第105-107行 |
