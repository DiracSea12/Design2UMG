### #26 Design2UMGSettings (1M 2L)

| 级别 | 问题 | 位置 |
|------|------|------|
| M | `FindClassOverride` 中 `NodeName.Contains(Rule.NamePattern)` 使用默认大小写敏感匹配。设计稿节点命名大小写不一致很常见（如 `btn_Login` vs `BTN_Login`），用户配置 pattern 后匹配不上会很困惑。建议显式使用 `ESearchCase::IgnoreCase`，或在 `FWidgetClassOverride` 上加一个 `bCaseSensitive` 开关 | `FindClassOverride` |
| L | `FindClassOverride` 对 `ClassOverrides` 数组做线性扫描，首个匹配即返回。当多条规则都能匹配同一节点名时，结果完全取决于数组顺序，但 UI 上没有任何优先级提示。规则少时无所谓，规则多了容易踩坑。考虑在 Category meta 或 ToolTip 中注明"首条匹配生效" | `FindClassOverride` |
| L | `FindClassOverride` 每次调用都对匹配到的 `TSoftClassPtr` 执行 `LoadSynchronous`。该函数在 `UMGWidgetUtils.cpp:353` 被逐节点调用，如果设计稿节点数量大且规则多，会产生大量重复的同步加载调用。虽然引擎内部有缓存不会真正重复加载，但调用开销仍可优化——可在导入开始时预加载一次，或用 `TMap<FString, UClass*>` 缓存已解析结果 | `FindClassOverride` 调用链 |
