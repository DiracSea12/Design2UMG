### #32 UMGPropertyApplier (2M 3L)

| 级别 | 问题 | 位置 |
|------|------|------|
| M | ImageMode "Crop" 的仿射矩阵解析逻辑在 `ApplyImageProps` 和 `ApplyBorderProps` 中完全重复（约20行），违反 DRY 原则。应提取为共用辅助函数 | `ApplyImageProps` L236-261, `ApplyBorderProps` L470-500 |
| M | `ApplyBorderProps` 中 `BrushColor` 分支为非 RoundedBox 的无纹理 Border 加载 WhiteSquareTexture，但 `ApplyTypedBorderBrush` 的 typed 路径没有对应逻辑。当 typed `bHasBrushColor=true` 且 Brush 无资源时，颜色会被设到透明画刷上，渲染结果与 legacy 路径不一致 | `ApplyTypedBorderBrush` L818-819 vs `ApplyBorderProps` L451-467 |
| L | `ApplyButtonProps` 中 Hovered 颜色用 `Color * 1.1f` 计算，这会把 Alpha 也乘以 1.1 导致超过 1.0（如输入 Alpha=1.0 → Hovered Alpha=1.1）。应单独处理 RGB 或 clamp Alpha | `ApplyButtonProps` L178 |
| L | `GetPropsApplierMap` 使用精确类匹配 + 继承链遍历查找，但映射表只注册了叶子类。如果用户创建了 UTextBlock 的自定义子类，`CastChecked<UTextBlock>` 能成功但匹配到的是父类条目，这本身没问题。但如果有人注册了一个中间类（如 UEditableTextBox 和 UMultiLineEditableTextBox 都是独立注册的），继承链上先命中哪个取决于 UE 的类层级，可能不符合预期。当前注册的类没有此冲突，但扩展时需注意 | `GetPropsApplierMap` L716-758 |
| L | `#define GET_PROP` 在匿名命名空间内定义后 `#undef`（L712），然后在 `ApplyCommonProperties` 中又重新 `#define`（L825）再 `#undef`（L846）。宏的两次定义/销毁增加维护负担，且宏名过于通用容易与其他翻译单元冲突。建议改用 inline lambda 或局部辅助函数替代 | 全文件，L49/L712/L825/L846 |
