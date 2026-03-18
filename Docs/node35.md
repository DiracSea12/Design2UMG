### #35 SDesign2UMGPreviewWindow (1M-资源 1L)

| 级别 | 问题 | 位置 |
|------|------|------|
| M-资源 | `CreatePreviewWidgetFromJson` 中 `ResolveTransientTextures` 下载远程图片并通过 `AddToRoot()` 阻止 GC 回收，但 `TextureMap` 是局部变量，函数结束后丢失引用，从未调用 `ReleaseTransientTextures` 释放。每次刷新预览都会泄漏一批 rooted 纹理，直到模块卸载时的全局清理才释放 | `CreatePreviewWidgetFromJson` 第 568-569 行 |
| L | `AsyncTask(ENamedThreads::GameThread, ...)` 使用了 `AsyncTask` 但未 `#include "Async/Async.h"`，依赖其他头文件的间接包含。间接包含不可靠，引擎头文件重构后可能编译失败 | `RefreshUMGPreview` 第 478 行 |
