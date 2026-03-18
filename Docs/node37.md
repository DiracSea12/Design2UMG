### #37 Design2UMGNotification (1M 2L)

| 级别 | 问题 | 位置 |
|------|------|------|
| M-API | `FAssetData(FSoftObjectPath(AssetPath).TryLoad())` 先同步加载对象再构造 FAssetData，仅为定位资产却将其完整加载到内存。应通过 `IAssetRegistry::GetAssetByObjectPath()` 从路径直接获取 FAssetData，避免不必要的对象加载 | `Success()` 第20行 |
| L | `Warning()` 使用 `CS_None` 完成状态，不会显示任何图标，与 Success(`CS_Success`) 和 Error(`CS_Fail`) 的明确视觉反馈不一致。Warning 缺少可辨识的视觉区分 | `Warning()` 第38行 |
| L-资源 | `Success()` 超链接回调中 `TryLoad()` 是同步加载，在 Game Thread 上执行。资产较大时可能造成短暂卡顿。影响有限因为是用户主动点击且单个资产 | `Success()` 第20行 lambda |
