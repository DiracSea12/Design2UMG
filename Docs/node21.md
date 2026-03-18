### #21 DesignRemoteImageImporter (2M 3L)

| 级别 | 问题 | 位置 |
|------|------|------|
| M | 缩进不一致：`BuildTextureAssetName`（L103-115）和 `BuildSourceKeyForLocal`（L128-134）函数签名多了一层缩进（2 tab），函数体内部又回到 1 tab，与同文件其他函数风格不统一。`ResolveTexturePath` 内本地文件分支 L509 处也有同样的多余缩进。`ResolveRemoteImages` 的 Summary 构建（L715）也多了一层。应统一为 1 tab | `BuildTextureAssetName`, `BuildSourceKeyForLocal`, `ResolveTexturePath`, `ResolveRemoteImages` |
| M | `BuildObjectPathFromImport`（L122-126）与 `BuildLongPackageName`+`BuildObjectPath`（L168-176）功能完全重复——前者是后两者的内联合体。两套函数共存增加维护负担，应删除 `BuildObjectPathFromImport`，调用处改用 `BuildObjectPath(BuildLongPackageName(...))` | 匿名命名空间辅助函数 |
| L | `DownloadUrl` 使用 `Flush(EHttpFlushReason::FullFlush)` 做同步等待（L224），会阻塞游戏线程直到所有 HTTP 请求完成。当前作为编辑器工具可接受，但如果未来需要批量下载多张图片，应考虑改为异步并发下载以提升导入速度 | `DownloadUrl` |
| L | `SanitizeAssetName`（L89-101）用 8 次 `ReplaceInline` 逐字符替换。可用 `FChar::IsAlnum` 遍历一次完成，或用 `ObjectTools::SanitizeObjectName`（UE 内置），更简洁且覆盖更多非法字符 | `SanitizeAssetName` |
| L | `AddReportMessage` 硬编码上限 32 条（L42），超出后静默丢弃。对于包含大量图片的设计文件，丢失的消息可能导致排查困难。建议至少在达到上限时追加一条截断提示 | `AddReportMessage` |
