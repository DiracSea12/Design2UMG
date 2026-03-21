### #34 UMGRenderUtils (2M 3L)

| 级别 | 问题 | 位置 |
|------|------|------|
| M | 文件名与 `#include` 不匹配：文件名为 `UMGRenderUtils.cpp`，但 `#include` 的是 `UMGWidgetUtils.h`，且不存在对应的 `UMGRenderUtils.h`。文件名暗示这是独立的渲染工具模块，实际上是 `FUMGWidgetUtils` 类的补充实现。应统一命名，要么重命名文件为 `UMGWidgetUtils_Render.cpp`，要么拆出独立的 `FUMGRenderUtils` 类 | 文件级 |
| M | `RenderWidgetToPng` 与 `RenderSlateWidgetToPngData` 存在大段重复代码（RT 创建配置、Renderer 设置、像素读取、PNG 编码，约 20 行几乎逐字相同）。`RenderWidgetToPng` 完全可以在构建好 SlateWidget 后直接调用 `RenderSlateWidgetToPngData`，消除重复 | `RenderWidgetToPng` / `RenderSlateWidgetToPngData` |
| L | `GetDesignerPreviewWindowSize` 函数已无调用方，是死代码。同样 `#include "Misc/ConfigCacheIni.h"` 和 `#include "WidgetBlueprintEditorUtils.h"` 仅为该死函数服务，属于多余 include | 匿名 namespace 第一个函数 |
| L | `ComputePreviewRenderSize` 第 50-51 行连续调用两次 `Window->SlatePrepass(1.0f)`，无注释说明原因。如果是刻意的双 pass（等布局稳定），应加注释；如果是笔误，应删除一次 | `ComputePreviewRenderSize` L50-51 |
| L | `ComputePreviewRenderSize` 接收 `Widget` 参数但从未使用其任何成员（仅做空判断后直接用 `FallbackSize`），函数签名误导。可移除 `Widget` 参数或直接内联到调用处 | `ComputePreviewRenderSize` |
