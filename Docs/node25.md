### #25 Design2UMGEditorModule (2M 3L)

| 级别 | 问题 | 位置 |
|------|------|------|
| M-维护 | 匿名命名空间承载了约 800 行业务逻辑（解析、导入、UI 对话框、异步流程），职责过重。`ImportFromFigma` 的三层嵌套异步回调尤其难读。建议将 Pen 导入、Figma 导入、UI 对话框拆分到独立的 `.cpp` 文件，匿名命名空间只留胶水代码 | 匿名 namespace (L45-L828) |
| M-风格 | 工具栏菜单注册中，前两个 `Section.AddMenuEntry`（ImportPenFile、ConnectToPencil）使用一级 Tab 缩进，后两个（ImportFromFigma、ImportLocalFigma）突然变成二级 Tab 缩进，同一个 lambda 体内缩进不一致 | `StartupModule` 工具栏注册 (L1096-L1131) |
| L-资源 | `GConnector`（TUniquePtr）和 `GFigmaClient`（TUniquePtr）是匿名命名空间的模块级全局变量，生命周期依赖 `ShutdownModule` 手动清理。如果模块异常卸载或热重载顺序不对，这些全局状态可能残留。考虑改为 `FDesign2UMGEditorModule` 的成员变量，随模块实例自动析构 | 匿名 namespace `GConnector` (L50), `GFigmaClient` (L346) |
| L-冗余 | `ConnectToPencilAndImport` 中 `bInProgress` 是 `static std::atomic<bool>`，但 UE5 HTTP 回调在 GameThread 执行，不存在真正的多线程竞争。`std::atomic` 在这里是多余的，普通 `bool` 即可，且语义更清晰 | `ConnectToPencilAndImport` (L196) |
| L-重复 | Figma 异步导入流程中，`FApp::IsUnattended()` + `RequestExit` 的模式重复出现 5 次（L355-359, L375-379, L387-391, L425-430, L437-442），每次都是相同的 log + exit。可提取为一个小工具函数如 `ExitIfUnattended(const TCHAR* Reason)` | `ImportFromFigma` 回调链 |
