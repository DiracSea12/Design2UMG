## H 级发现核实 - Batch 4

| # | 发现 | 结论 | 理由 |
|---|------|------|------|
| 28-H | `IncrementalUpdater.cpp` 中 `FilterByAccepted` 对 Removed 条目的拒绝是空操作，旧节点无法保留回新树 | ✅ 确认 | `FilterByAccepted` 接收的是新树（`Data.RootWidget`），而 Removed 条目的 NodeId 来自旧树中存在但新树中不存在的节点（`ComputeDiff` L110-116）。当用户取消勾选 Removed 条目（意图保留旧节点），该 Id 进入 `RejectedIds`，但新树中根本没有这个 Id 的节点，过滤无任何效果。用户的"拒绝删除"操作被静默忽略，旧节点不会被保留。 |
| 36-H | `DesignImportUtils.cpp` 中 `GenerateBlueprintAndPng` 用原始 `OutputPath` 而非已解析的 `FinalPath` 提取 PNG 文件名，`OutputPath` 为空时文件名丢失 | ✅ 确认 | L89 已通过 `ResolveOutputPath` 得到 `FinalPath`（空值回退到 `/Game/Design2UMG/PenConvert_Output`），但 L118 `FPaths::GetBaseFilename(OutputPath)` 仍使用原始参数。当 `OutputPath` 为空时，`GetBaseFilename("")` 返回空串，PNG 文件名变为 `.png`（无名称前缀）。应改为 `FPaths::GetBaseFilename(FinalPath)`。 |
| 38-H1 | `Design2UMGTests.cpp` Test 12 硬编码绝对路径 `G:/UE5/FPSRoguelike/Designs/pencil-welcome.pen` | ✅ 确认 | L819 明确硬编码 `TEXT("G:/UE5/FPSRoguelike/Designs/pencil-welcome.pen")`。该路径仅在特定开发机器上有效，其他环境（CI、协作者机器）必然找不到文件导致测试失败。应使用相对路径或测试数据目录宏。 |
| 38-H2 | `Design2UMGTests.cpp` Test 12 向 `/Game/Design2UMG/PencilWelcome` 写入真实资产，无清理逻辑 | ✅ 确认 | L820 设定 `OutputPath = "/Game/Design2UMG/PencilWelcome"`，L837 循环调用 `GenerateBlueprintAndPng` 生成 Widget Blueprint 和 PNG 到该路径下。函数结束（L840 `return true`）前无任何资产删除或清理逻辑，测试产物会永久残留在项目 Content 目录中，污染资产库。 |
