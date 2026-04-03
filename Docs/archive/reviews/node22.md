### #22 FigmaImageExporter (1M-API 1L)

| 级别 | 问题 | 位置 |
|------|------|------|
| M-API | Figma images API 对 `ids` 参数有 URL 长度限制（约 2000 字符），大型文档可能包含数百个向量节点，`ExportVectorNodes` 将所有 ID 一次性拼接传入 `ExportImages`，未做分批处理，超限时请求会被截断或返回 400 | `ExportVectorNodes` → `Client.ExportImages(FileKey, VectorIds, ...)` (L135) |
| L | `CollectVectorNodeIds` 是文件级 static 自由函数，与类方法 `ApplyExportedImages` 做同样的递归遍历且过滤条件相同（`EPenNodeType::Path`），但两者独立实现，过滤逻辑如果将来变更需要同步两处。可考虑提取为共用的 visitor 或至少放入类内作为 private static | `CollectVectorNodeIds` (L87) vs `ApplyExportedImages` (L98) |

**说明**：

1. **M-API — 未分批调用 Figma API**：`FigmaHttpClient::ExportImages` 内部用逗号拼接所有 NodeId 构造 URL query string（见 `FigmaHttpClient.cpp:25`：`?ids=%s&format=png&scale=2`）。Figma REST API 对单次请求的 `ids` 数量没有官方硬限制，但 HTTP URL 总长度受服务端和中间代理限制（通常 2000-8000 字符）。一个 Figma NodeId 格式为 `123:456`，平均约 7 字符加逗号分隔符，200+ 个向量节点就可能触及上限。建议在 `ExportVectorNodes` 中按批次（如每批 50 个）分组调用。

2. **L — 收集与应用的遍历逻辑重复**：`CollectVectorNodeIds`（匿名 static）和 `ApplyExportedImages`（类 static）都递归遍历节点树并以 `EPenNodeType::Path` 为条件过滤。当前代码量小，不是紧急问题，但如果过滤条件变更（比如增加 `BooleanOperation` 类型），需要记得同步两处。
