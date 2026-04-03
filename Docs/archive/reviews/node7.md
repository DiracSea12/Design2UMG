### #7 PenVariableResolver (1H 2M 2L)

| 级别 | 问题 | 位置 |
|------|------|------|
| H | 变量循环引用导致无限递归栈溢出：`$A→$B→$A` 时 `ResolveString` 无限递归，缺少深度限制或已访问集合 | `ResolveString` → `ResolveThemedValue` 循环 |
| M | 圆角数组元素不足4个时静默丢弃数据：`[8, 0, 8]` 等1-3元素数组会返回全零默认值（`bUniform=true, Uniform=0`），无日志提示 | `ResolveCornerRadius` 第125行 `if (Arr.Num() >= 4)` |
| M | `ResolveNumber` 对非数值字符串静默返回0：颜色变量 `"#FF0000"` 误用在数值上下文时 `FCString::Atof` 返回0.0f，无警告，难以排查 | `ResolveNumber` 第86行 |
| L | `IsVariableRef` 过于宽松：单独的 `"$"` 也被判定为变量引用，`Mid(1)` 产生空字符串去查表 | `IsVariableRef` 第32行 |
| L-API | `Variables` 裸指针持有外部 TMap，生命周期由调用方保证，无所有权语义表达（如注释或类型约束） | 类成员 `Variables` 指针 |
