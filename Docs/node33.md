### #33 UMGSlotApplier (1M 2L)

| 级别 | 问题 | 位置 |
|------|------|------|
| M | `ApplyScrollBoxSlot` 与 `ApplyBoxSlotCommon` 存在重复的 Padding/HAlign/VAlign + SizeRule/Fill 逻辑，ScrollBoxSlot 完全可以复用 `ApplyBoxSlotCommon` 模板（`UScrollBoxSlot` 同样有 `SetPadding`/`SetSize`/`SetHorizontalAlignment`/`SetVerticalAlignment`），当前是手动复制粘贴，未来改动需同步两处 | `ApplyScrollBoxSlot` (L107-120) vs `ApplyBoxSlotCommon` (L44-57) |
| L | `ApplySlotProperties` 中 `bHasNonDefaultCanvas` 的 10 行浮点比较链可读性差，可提取为 `FUMGSlotData::HasNonDefaultCanvasFields()` 之类的辅助方法，与已有的 `IsNearlyEqualMargin` 风格保持一致 | `ApplySlotProperties` (L211-221) |
| L | `#include "UMGWidgetUtils.h"` 作为本文件的主 include 应该放在第一行（UE5 IWYU 惯例：对应头文件优先），当前文件名是 `UMGSlotApplier.cpp` 但没有同名头文件，若未来拆分出 `UMGSlotApplier.h`，include 顺序需要调整；目前不影响编译但不符合命名对应惯例 | 文件顶部 (L3) |
