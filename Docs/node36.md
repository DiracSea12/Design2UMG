### #36 DesignImportUtils (1H 1M 1L)

| 级别 | 问题 | 位置 |
|------|------|------|
| H | `GenerateBlueprintAndPng` 第118行用原始 `OutputPath` 而非已解析的 `FinalPath` 提取 PNG 文件名。当 `OutputPath` 为空时（触发 fallback），`FPaths::GetBaseFilename("")` 返回空串，PNG 保存为 `Saved/Design2UMG/.png`，文件名丢失 | `GenerateBlueprintAndPng` L118 |
| M | `FGenerateCallback` 类型别名（.h L39）已声明但从未使用。`OpenPreviewWindow` 签名中没有回调参数，内部硬编码调用 `GenerateFromEditedJson`，调用者无法自定义生成行为。属于死代码，且与注释 `@param OnGenerate` 描述的可定制设计不符 | `OpenPreviewWindow` / .h L34-43 |
| L | `GenerateBlueprintAndPng` 跳过了增量更新（`IncrementalUpdater`）和 `FImportResult` 汇总通知，而 `GenerateBlueprint` 两者都有。两条生成路径行为不一致，用户走 PNG 路径时既看不到 diff 预览也收不到导入统计 | `GenerateBlueprintAndPng` vs `GenerateBlueprint` |
