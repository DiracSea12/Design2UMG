### #23 FontResolver (2H 3M 1L)

| 级别 | 问题 | 位置 |
|------|------|------|
| H | `FindMissingFonts` 与 `FindFontAssetPath` 字体匹配逻辑不一致。`ScanProjectFontAssets` 只收集原始 `AssetName` 并用 `TSet::Contains` 做区分大小写的精确匹配；而 `FindFontAssetPath` 做了忽略大小写 + 空格转下划线的模糊匹配。结果：字体实际存在但 `FindMissingFonts` 仍误报缺失，触发错误的用户警告 | `FindMissingFonts` / `ScanProjectFontAssets` (L78-86 vs L115-130) |
| H | `DownloadToFile` 在编辑器主线程上同步阻塞（`Sleep` + `Tick` 循环最多 30 秒）。下载期间编辑器 UI 完全冻结，用户体验极差，且网络慢时可能触发 Windows "未响应"弹窗 | `DownloadToFile` (L158-168) |
| M | `ResolveFont` 从 `RemoteImageImportPath`（图片导入路径）推导字体存储路径，语义耦合错误。图片路径和字体路径是两个独立概念，用户修改图片路径会意外影响字体存储位置。应在 Settings 中增加独立的 `FontImportPath` 配置项 | `ResolveFont` (L180-184) |
| M | `ResolveFont` 下载远程字体时硬编码 `.ttf` 扩展名（L210），但字体 URL 可能指向 `.otf`、`.woff2` 等格式。应从 URL 或 HTTP Content-Type 推断实际格式，否则后续 FontFace 加载可能因扩展名不匹配而出现问题 | `ResolveFont` (L210) |
| M | 静态成员 `FontAssetCache` 在模块卸载/热重载时不会自动清理。如果编辑器热重载插件，缓存中的旧路径可能指向已失效的资产，导致后续 `LoadObject` 静默失败 | `FontAssetCache` 静态生命周期 |
| L | `FindFontAssetPath` 和 `ScanProjectFontAssets` 各自独立调用 `GetAssetsByClass` 全量扫描项目字体资产。在同一次导入流程中 `FindMissingFonts` 调用 `ScanProjectFontAssets`，随后 `Design2UMGConverter` 逐节点调用 `FindFontAssetPath` 又重复扫描。应统一走缓存路径，避免重复的 AssetRegistry 查询 | `FindFontAssetPath` / `ScanProjectFontAssets` |
