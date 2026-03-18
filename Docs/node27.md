### #27 DesignSourceRegistry (1M 2L)

| 级别 | 问题 | 位置 |
|------|------|------|
| M-API | `Register()` 使用 `TMap::Add()` 会静默覆盖已有同名 SourceId 的注册，调用方无法感知冲突。如果两个模块注册同一个 SourceId，后者直接覆盖前者且无任何警告，可能导致难以排查的行为变更 | `Register()` |
| L-API | 类缺少 `DESIGN2UMGEDITOR_API` 导出宏。当前所有调用方都在同模块内所以没问题，但头文件放在 `Public/` 目录暗示这是对外接口，外部模块若 include 此头文件将链接失败 | `class FDesignSourceRegistry` 声明 |
| L | `GetRegisteredSources()` 返回 `TArray<FName>` 按值拷贝，当前注册量极小（2个）无性能问题，但接口设计上如果未来注册源增多，可考虑返回 `const TMap&` 的迭代或接受 visitor 回调 | `GetRegisteredSources()` |
