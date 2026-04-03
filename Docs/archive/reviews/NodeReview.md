# Design2UMG 全节点 Code Review

> 审查维度：正确性、资源生命周期、线程安全、API 设计
> 不审查：防御式编程（空指针检查、边界检查等）
> 日期：2026-02-27

## Batch 1 — 解析层
| # | 节点 | 文件 | 状态 |
|---|------|------|------|
| 1 | PenParser | PenParser.h/.cpp | ✅ 3M 2L |
| 2 | FigmaParser | FigmaParser.h/.cpp | ✅ 2M 2L (+2M API) |
| 3 | PenImporter | PenImporter.h/.cpp | ✅ 2M 2L |
| 4 | PenFileImportFactory | PenFileImportFactory.h/.cpp | ✅ 1H 2M 1L (+1M资源) |
| 5 | PenMCPBridge | PenMCPBridge.h/.cpp | ✅ 1H 2M 1L |
| 6 | PencilConnector | PencilConnector.h/.cpp | ✅ 1H 2M 1L |

## Batch 2 — 变换层 + 转换层
| # | 节点 | 文件 | 状态 |
|---|------|------|------|
| 7 | PenVariableResolver | PenVariableResolver.h/.cpp | ✅ 1H 2M 2L |
| 8 | RefExpander | RefExpander.h/.cpp | ✅ 2H 2M 1L |
| 9 | PenTypeMapper | PenTypeMapper.h/.cpp | ✅ 1M 2L |
| 10 | Design2UMGConverter | Design2UMGConverter.h/.cpp | ✅ 2H 3M 1L |
| 11 | VisualMapper | VisualMapper.h/.cpp | ✅ 2H 2M 2L |
| 12 | LayoutMapper | LayoutMapper.h/.cpp | ✅ 无发现 |

## Batch 3 — 布局 + 输出
| # | 节点 | 文件 | 状态 |
|---|------|------|------|
| 13 | LayoutCalculator | LayoutCalculator.h/.cpp | ✅ 1M 2L |
| 14 | LayoutFixer | LayoutFixer.h/.cpp | ✅ 1H 2M 2L |
| 15 | LayoutValidator | LayoutValidator.h/.cpp | ✅ 1H 1M 1L |
| 16 | UMGGenerator | UMGGenerator.h/.cpp | ✅ 1M 2L |
| 17 | UMGJsonParser | UMGJsonParser.h/.cpp | ✅ 1M 2L |
| 18 | UMGJsonSerializer | UMGJsonSerializer.h/.cpp | ✅ 1H 1M |

## Batch 4 — 基础设施
| # | 节点 | 文件 | 状态 |
|---|------|------|------|
| 19 | MCPHttpClient | MCPHttpClient.h/.cpp | ✅ 1H 2M 2L |
| 20 | FigmaHttpClient | FigmaHttpClient.h/.cpp | ✅ 1H 2M 1L |
| 21 | DesignRemoteImageImporter | DesignRemoteImageImporter.h/.cpp | ✅ 2M 3L |
| 22 | FigmaImageExporter | FigmaImageExporter.h/.cpp | ✅ 1M-API 1L |
| 23 | FontResolver | FontResolver.h/.cpp | ✅ 2H 3M 1L |
| 24 | PenPathRasterizer | PenPathRasterizer.h/.cpp | ✅ 1H 2M 2L |

## Batch 5 — 编辑器 + 生成
| # | 节点 | 文件 | 状态 |
|---|------|------|------|
| 25 | Design2UMGEditorModule | Design2UMGEditorModule.h/.cpp | ✅ 2M 3L |
| 26 | Design2UMGSettings | Design2UMGSettings.h/.cpp | ✅ 1M 2L |
| 27 | DesignSourceRegistry | DesignSourceRegistry.h/.cpp | ✅ 1M 2L |
| 28 | IncrementalUpdater | IncrementalUpdater.h/.cpp | ✅ 1H 2M 1L |
| 29 | ProceduralShapeGenerator | ProceduralShapeGenerator.h/.cpp | ✅ 2M 4L |
| 30 | PreviewUserWidget | PreviewUserWidget.h/.cpp | ✅ 无发现 |

## Batch 6 — 工具 + UI
| # | 节点 | 文件 | 状态 |
|---|------|------|------|
| 31 | UMGWidgetUtils | UMGWidgetUtils.h/.cpp | ✅ 2M 3L |
| 32 | UMGPropertyApplier | UMGPropertyApplier.cpp | ✅ 2M 3L |
| 33 | UMGSlotApplier | UMGSlotApplier.cpp | ✅ 1M 2L |
| 34 | UMGRenderUtils | UMGRenderUtils.cpp | ✅ 2M 3L |
| 35 | SDesign2UMGPreviewWindow | SDesign2UMGPreviewWindow.h/.cpp | ✅ 1M-资源 1L |
| 36 | DesignImportUtils | DesignImportUtils.h/.cpp | ✅ 1H 1M 1L |

## Batch 7 — 通知 + 测试
| # | 节点 | 文件 | 状态 |
|---|------|------|------|
| 37 | Design2UMGNotification | Design2UMGNotification.h/.cpp | ✅ 1M 2L |
| 38 | Design2UMGTests | Tests/Design2UMGTests.cpp | ✅ 2H 3M 1L |

---

## 发现汇总

### #1 PenParser (3M 2L)

| 级别 | 问题 | 位置 |
|------|------|------|
| M | ParseSize: `End` 从字符串起始搜索而非 `Start` 之后，可能匹配到错误位置 | ParseSize() |
| M | ParseSize: 字符串路径用 `NumVal > 0` 判断，与数值路径逻辑不一致 | ParseSize() |
| M | ParseDocumentInternal: themes 值未检查类型就调用 AsArray() | ParseDocumentInternal() |
| L | ExtractTopFrames: 自由函数缺少导出宏 | ExtractTopFrames() |
| L | ParseNodeExtended: "effect" vs "effects" 键优先级静默丢弃数据 | ParseNodeExtended() |

### #2 FigmaParser (2M 2L + 2M API)

| 级别 | 问题 | 位置 |
|------|------|------|
| M | flip 分解逻辑在 CosFromX*CosFromY >= 0 时计算错误 | 矩阵分解 |
| M | ParseSize: EffectiveBounds 回退到 VisualBounds 会破坏 Auto Layout | ParseSize() |
| M API | ParseFill/ParseFills 冗余，Node->Fill 是 Fills.Last() 的多余拷贝 | ParseFill/ParseFills |
| M API | ParseNode ParentObj nullptr 契约不明确 | ParseNode() |
| L | 黑色遮罩矩形启发式判断过于激进 | 遮罩检测 |
| L | ExtractFileKey 正则不匹配含连字符的 key | ExtractFileKey() |

### #3 PenImporter (2M 2L)

| 级别 | 问题 | 位置 |
|------|------|------|
| M | AutoImportFonts/FindMissingFonts 绕过取消检查 | 字体导入流程 |
| M | WarningCount 与 Warnings.Num() 冗余，可能不同步 | 警告统计 |
| L API | ImportFromFile vs ImportFileWithTargetNode 职责重叠 | 公共接口 |
| L | ShowSummaryDialog 窗口无父窗口 | UI |

### #4 PenFileImportFactory (1H 2M 1L + 1M资源)

| 级别 | 问题 | 位置 |
|------|------|------|
| H | ImportSingleFrame nullptr 有两种含义，都设 bOutOperationCanceled=true | ImportSingleFrame() |
| M | ExtractTopFrames 声明/实现来源不明确 | ExtractTopFrames |
| M | 同一文件通过不同路径被解析两次 | 导入流程 |
| M资源 | 异步 lambda 捕获可能过期的 OutputPath | 异步导入 |
| L API | ImportWithTargetNode 返回 void，丢弃结果 | 公共接口 |

### #5 PenMCPBridge (1H 2M 1L)

| 级别 | 问题 | 位置 |
|------|------|------|
| H | BuildDocumentJson 接收 VarsObj 而非 SourceObj，version 始终为 "2.6" | BuildDocumentJson() |
| M | FilterWithReusables 去重未检查祖先包含关系 | FilterWithReusables() |
| M API | AssembleDocumentJson 导致三次 JSON 序列化往返 | AssembleDocumentJson() |
| L | TryGetField 共享引用而非深拷贝 | TryGetField() |

### #6 PencilConnector (1H 2M 1L)

| 级别 | 问题 | 位置 |
|------|------|------|
| H资源 | FindAvailablePort TOCTOU 竞态：socket 释放与进程绑定之间有窗口 | FindAvailablePort() |
| M | Launch 阻塞主线程最多 10 秒 | Launch() |
| M | FindAvailablePort 回退返回可能不可用的 StartPort | FindAvailablePort() |
| M API | ServerProcess 声明为 mutable 无必要 | 类声明 |
| L | DetectAppName 子串匹配不一致 | DetectAppName() |

### #7 PenVariableResolver (1H 2M 2L)

| 级别 | 问题 | 位置 |
|------|------|------|
| H | 变量循环引用导致无限递归栈溢出：`$A→$B→$A` 时无深度限制或已访问集合 | ResolveString → ResolveThemedValue 循环 |
| M | 圆角数组元素不足4个时静默丢弃数据，返回全零默认值 | ResolveCornerRadius L125 |
| M | ResolveNumber 对非数值字符串静默返回0，无警告 | ResolveNumber L86 |
| L | IsVariableRef 过于宽松，单独 `"$"` 也被判定为变量引用 | IsVariableRef L32 |
| L API | Variables 裸指针持有外部 TMap，无所有权语义表达 | 类成员 Variables |

### #8 RefExpander (2H 2M 1L)

| 级别 | 问题 | 位置 |
|------|------|------|
| H | DeepCopyNode 只深拷贝 Children，遗漏 Descendants，拷贝与源共享指针，MergeDescendants 会污染源数据 | DeepCopyNode L232-239 |
| H | ApplyDescendantOverride 对象替换直接插入外部指针未做 DeepCopy，多实例互相串改 | ApplyDescendantOverride L276 |
| M | Children 赋值是 TSharedPtr 数组浅拷贝，Target 与源共享子节点指针 | ApplyOverrideProperties L72, ApplyInstanceOverrides L173 |
| M | JustifyContent/AlignItems/TextAlign 用枚举默认值判断"未设置"，无法显式重置回默认值 | ApplyOverrideProperties L61-66 |
| L | ApplyOverrideProperties 与 ApplyInstanceOverrides 覆盖的属性集不一致，语义边界不清晰 | 两函数对比 |

### #9 PenTypeMapper (1M 2L)

| 级别 | 问题 | 位置 |
|------|------|------|
| M | MapNodeTypeToWidget 未处理 Ref 枚举值，静默落入 default 返回 CanvasPanel | MapNodeTypeToWidget switch default |
| L | MapFontWeight 18次 if-else 字符串比较，可用静态 TMap 替代 | MapFontWeight |
| L | Text 和 IconFont 两个 case 返回相同 TextBlock，可合并 fall-through | MapNodeTypeToWidget L22-27 |

### #10 Design2UMGConverter (2H 3M 1L)

| 级别 | 问题 | 位置 |
|------|------|------|
| H | Convert 通过 TSharedPtr 绕过 const FDesignDocument& 语义，静默修改调用者原始数据，多次调用结果不一致 | Convert, PreComputeFitContentSizes, ConvertNode |
| H | static TSet WarnedFamilies 非线程安全，同文件已用 thread_local 说明有多线程预期 | CreateIconFontWidget L752 |
| M | Ref 子节点在循环中手动展开，而 ConvertNode 内部已有完整 Ref 展开逻辑，冗余路径 | ConvertNode 子节点循环 L549-563 |
| M | CreateIconFontWidget 转换过程中调用 SaveConfig() 持久化到磁盘，违反单一职责 | CreateIconFontWidget L739 |
| M | 文本尺寸估算硬编码魔数 1.5f/0.6f，对中文偏差极大，散布两处 | EstimateNodeSize, PreComputeFitContentSizes |
| L | Rectangle 与 Frame/Group 处理完全相同，可合并 fall-through | ConvertNode switch |

### #11 VisualMapper (2H 2M 2L)

| 级别 | 问题 | 位置 |
|------|------|------|
| H | Angular 渐变被当作 Radial 处理，fallthrough 到 Radial 分支，渲染结果完全不同 | ApplyFillProperties L73-75, WrapWithMultiFill L491-493 |
| H | 多 Shadow 效果只保留最后一个，同 key 覆盖前者无警告 | ApplyVisualProperties L166-199 |
| M | BuildGradientMaterialParams 只取首尾两色，多色渐变中间色全部丢弃 | BuildGradientMaterialParams L21-22 |
| M | WrapWithBoxShadow 中 Overlay 接管属性但未转移 Brush 数据 | WrapWithBoxShadow L397-401 |
| L | 渐变方向判定逻辑在两处完全重复，违反 DRY | ApplyFillProperties L80-84, WrapWithMultiFill L498-501 |
| L | WrapWithBorder 属性迁移使用四处硬编码 key 列表 | WrapWithBorder L282-341 |

### #12 LayoutMapper (无发现)

| 级别 | 问题 | 位置 |
|------|------|------|

代码简洁清晰，职责单一，各函数逻辑验证无误。

### #13 LayoutCalculator (1M 2L)

| 级别 | 问题 | 位置 |
|------|------|------|
| M | CalculateAxis 中 TOptional<float> 按值传递产生不必要拷贝 | CalculateAxis 签名 |
| L | SelectAnchorWithHysteresis else 兜底分支风格不一致 | SelectAnchorWithHysteresis L35 |
| L | bEnableNineGrid=false 时 Anchor 硬编码 0.0f 缺少意图注释 | CalculateAxis L105-107 |

### #14 LayoutFixer (1H 2M 2L)

| 级别 | 问题 | 位置 |
|------|------|------|
| H | FixEqualSpacing 边界检查硬编码 0.0~1.0 归一化范围，像素坐标输入时等距功能静默失效 | FixEqualSpacing L470/489 |
| M | 吸附用 OriginalBounds 参考，等距写入 ProposedBounds，两个 pass 数据源不一致 | FixChildrenSpacing L553-612 |
| M | DetectRows/DetectColumns 只与首元素比较重叠度，渐变尺寸场景下分组碎片化 | DetectRows L340, DetectColumns L380 |
| L | 偏差判断用 AvgGap，实际修正用 MedianGap，标准不一致 | FixEqualSpacing L438-446 |
| L | GetMedian 对输入数组做完整拷贝+排序，循环调用时有冗余开销 | GetMedian L85 |

### #15 LayoutValidator (1H 1M 1L)

| 级别 | 问题 | 位置 |
|------|------|------|
| H | FBounds 定义为像素坐标，但误差计算函数当归一化值使用（乘以 DesignSize），接口契约矛盾 | CalculateBoundsError/CalculatePosError L11-28 |
| M | CalculatePosError 已实现但整个插件无调用点，死代码 | CalculatePosError |
| L | EvalThresholds 命名空间和常量命名风格不符合 UE5 惯例 | EvalThresholds L9-13 |

### #16 UMGGenerator (1M 2L)

| 级别 | 问题 | 位置 |
|------|------|------|
| M | EffectiveDesignSize 是 static 全局变量，并发场景下后一次调用覆盖前一次 | BuildWidgetBlueprint L98 |
| L | DumpNode 缩进字符串循环拼接，深层节点大量临时分配 | DumpNode lambda L106-107 |
| L | 冲突重命名循环退出条件不直观，Suffix 值语义模糊 | BuildWidgetBlueprint L38-53 |

### #17 UMGJsonParser (1M 2L)

| 级别 | 问题 | 位置 |
|------|------|------|
| M | ParseWidgetNode 直接属性解析与 ParseProperties 高度重复但行为不一致，前者不处理 Array | ParseWidgetNode vs ParseProperties |
| L | 同名 key 时直接属性静默覆盖 Properties 对象值，优先级语义隐含 | ParseWidgetNode L135-173 |
| L | ParseAnchorData 中 float 字段匹配 double 重载，隐式窄化 | ParseAnchorData L358-359 |

### #18 UMGJsonSerializer (1H 1M)

| 级别 | 问题 | 位置 |
|------|------|------|
| H数据丢失 | SerializeSlot 未序列化 bAutoSize 字段，往返后 Size To Content 静默丢失 | SerializeSlot() |
| M数据丢失 | SerializeWidgetNode 未序列化 TextStyle/Brush v2.4 类型化结构体 | SerializeWidgetNode() |

### #19 MCPHttpClient (1H 2M 2L)

| 级别 | 问题 | 位置 |
|------|------|------|
| H并发 | NextRequestId 无锁递增，CallToolAsync 回调在 HTTP 线程，与 Game Thread 交叉时数据竞争 | NextRequestId |
| M重复 | CallToolAsync 手动重建 JSON-RPC 请求构造，与 SendRequest 高度重复 | CallToolAsync vs SendRequest |
| M API | CallToolAsync 没有 SetTimeout，服务端无响应时请求永远挂起 | CallToolAsync |
| L | 同步等待循环中 Slate::Tick() 可能引发 UI 重入 | SendRequest 等待循环 |
| L | SetNumberField 存 id 用 double，超 2^53 精度丢失 | SendRequest/CallToolAsync |

### #20 FigmaHttpClient (1H 2M 1L)

| 级别 | 问题 | 位置 |
|------|------|------|
| H API | NodeIds 未对冒号做 URL 编码/替换，与 Figma 官方要求的 `0-1` 格式不一致 | ExportImages URL 拼接 |
| M资源 | HTTP 异步回调无取消机制，模块卸载时捕获的外部对象可能已失效 | SendGet lambda 捕获 |
| M | FigmaApiBase 是 file-scope static FString，受 Static Initialization Order Fiasco 影响 | cpp:10 |
| L | Token 明文 FString 长期驻留内存，无使用后清除机制 | Token 成员变量 |

### #21 DesignRemoteImageImporter (2M 3L)

| 级别 | 问题 | 位置 |
|------|------|------|
| M | 缩进风格不一致，多处函数签名多了一层缩进 | BuildTextureAssetName, BuildSourceKeyForLocal 等 |
| M | BuildObjectPathFromImport 与 BuildLongPackageName+BuildObjectPath 功能完全重复 | 匿名命名空间辅助函数 |
| L | DownloadUrl 用 Flush 同步阻塞游戏线程 | DownloadUrl |
| L | SanitizeAssetName 用8次 ReplaceInline，可用 UE 内置函数简化 | SanitizeAssetName |
| L | AddReportMessage 硬编码上限32条，超出静默丢弃 | AddReportMessage |

### #22 FigmaImageExporter (1M-API 1L)

| 级别 | 问题 | 位置 |
|------|------|------|
| M API | 所有向量节点 ID 一次性拼入 URL，大型文档可能超 URL 长度限制 | ExportVectorNodes L135 |
| L | CollectVectorNodeIds 与 ApplyExportedImages 独立实现相同递归遍历+过滤 | 两函数对比 |

### #23 FontResolver (2H 3M 1L)

| 级别 | 问题 | 位置 |
|------|------|------|
| H | FindMissingFonts 精确匹配 vs FindFontAssetPath 模糊匹配，逻辑不一致导致误报缺失 | FindMissingFonts/ScanProjectFontAssets |
| H | DownloadToFile 主线程同步阻塞最多30秒，编辑器冻结 | DownloadToFile L158-168 |
| M | ResolveFont 从图片导入路径推导字体存储路径，语义耦合错误 | ResolveFont L180-184 |
| M | 下载远程字体硬编码 .ttf 扩展名，不支持 .otf/.woff2 | ResolveFont L210 |
| M | FontAssetCache 静态生命周期，热重载时不清理 | FontAssetCache |
| L | FindFontAssetPath 和 ScanProjectFontAssets 各自独立全量扫描字体资产 | 两函数对比 |

### #24 PenPathRasterizer (1H 2M 2L)

| 级别 | 问题 | 位置 |
|------|------|------|
| H资源 | Outer 参数从未使用，纹理始终创建在 TransientPackage，接口误导调用方 | Rasterize() 签名 |
| M | MakeCacheKey 用 uint32 哈希做缓存键，碰撞后返回错误纹理 | MakeCacheKey/GPathCache |
| M | ScanlineFill 每条扫描线暴力遍历全部边+分配临时数组，大路径开销显著 | ScanlineFill 内层循环 |
| L | 头文件注释漏列 S/s T/t 指令，实现中已支持 | PenPathRasterizer.h 注释 |
| L API | ParsePathData implicit lineto 逐字符跳过而非跳过完整数字 | ParsePathData L188 |

### #25 Design2UMGEditorModule (2M 3L)

| 级别 | 问题 | 位置 |
|------|------|------|
| M维护 | 匿名命名空间承载约800行业务逻辑，职责过重 | 匿名 namespace L45-L828 |
| M风格 | 工具栏菜单注册中缩进不一致 | StartupModule 工具栏注册 |
| L资源 | GConnector/GFigmaClient 全局变量依赖手动清理 | 匿名 namespace |
| L冗余 | bInProgress 用 std::atomic 但回调在 GameThread，多余 | ConnectToPencilAndImport |
| L重复 | ExitIfUnattended 模式重复出现5次 | ImportFromFigma 回调链 |

### #26 Design2UMGSettings (1M 2L)

| 级别 | 问题 | 位置 |
|------|------|------|
| M | FindClassOverride 大小写敏感匹配，设计稿命名不一致时匹配失败 | FindClassOverride |
| L | 多条规则匹配同一节点时首条命中即返回，无优先级提示 | FindClassOverride |
| L | 逐节点调用 LoadSynchronous，可预加载或缓存优化 | FindClassOverride 调用链 |

### #27 DesignSourceRegistry (1M 2L)

| 级别 | 问题 | 位置 |
|------|------|------|
| M API | Register() 用 TMap::Add 静默覆盖同名 SourceId，无冲突警告 | Register() |
| L API | 类缺少 DESIGN2UMGEDITOR_API 导出宏，外部模块链接失败 | 类声明 |
| L | GetRegisteredSources() 按值返回 TArray | GetRegisteredSources() |

### #28 IncrementalUpdater (1H 2M 1L)

| 级别 | 问题 | 位置 |
|------|------|------|
| H | FilterByAccepted 对 Removed 条目的拒绝是空操作，旧节点无法保留回新树 | FilterByAccepted |
| M | IsNodeModified 只比较类型和名称，属性变更全部漏检 | IsNodeModified |
| M | TMap::Add 重复 ID 时静默覆盖，diff 结果不可靠 | CollectExistingIds/CollectNewIds |
| L | 三个 Count 函数各自遍历一次数组，可合并单次遍历 | CountAdded/Modified/Removed |

### #29 ProceduralShapeGenerator (2M 4L)

| 级别 | 问题 | 位置 |
|------|------|------|
| M | Outer 参数是死代码，CreateTransient 不接受 Outer，整条调用链无效 | CreateTransientTexture 及所有 Generate* |
| M | 头文件注释说 GC 自动处理，但实现用 AddToRoot 阻止 GC，必须手动释放 | .h 注释 vs AddToRoot |
| L | ColorStart/ColorEnd 变量同时承载线性和径向渐变颜色，语义不匹配 | GenerateFromMaterialRef |
| L | GenerateRing Thickness>=0.5 时内半径<=0，有效范围未约束 | GenerateRing L95 |
| L | GenerateRoundedRect 四角圆心计算不对称，小纹理差一像素 | GenerateRoundedRect L146-163 |
| L性能 | FillTextureData 每次调用 FlushRenderingCommands，批量生成时反复阻塞 | FillTextureData L51 |

### #30 PreviewUserWidget (无发现)

| 级别 | 问题 | 位置 |
|------|------|------|

代码极简，职责单一，无可改进之处。

### #31 UMGWidgetUtils (2M 3L)

| 级别 | 问题 | 位置 |
|------|------|------|
| M | `EffectiveDesignSize` 是可变全局静态变量，多处读写无同步机制，作为公共 API 暴露可变全局状态是不良设计 | `FUMGWidgetUtils::EffectiveDesignSize` |
| M | `HasValidSlotHistory` 将 Offsets 默认值硬编码为 `(0,0,100,100)`，但 `FUMGSlotData` 默认构造是全零，导致全默认 Slot 被误判为有历史数据 | `HasValidSlotHistory()` L509-521 |
| L | `ParseColor` 对 `#RGB` 短格式展开后传无 `#` 的 Hex，对 `#RRGGBB` 直接传带 `#` 的原始字符串，两条路径风格不一致 | `ParseColor()` L826-832 |
| L | `BuildAutoGridSlots` 排序 lambda 用 `IsNearlyEqual` 容差判断同行，违反严格弱序传递性 | `BuildAutoGridSlots()` 排序 lambda L269-276 |
| L | `CalculateDesignSize` 对拉伸锚点节点完全跳过尺寸计算，可能导致设计尺寸偏小 | `TraverseNodeForDesignSize()` L1033-1067 |

### #32 UMGPropertyApplier (2M 3L)

| 级别 | 问题 | 位置 |
|------|------|------|
| M | ImageMode "Crop" 的仿射矩阵解析逻辑在 `ApplyImageProps` 和 `ApplyBorderProps` 中完全重复（约20行），违反 DRY | `ApplyImageProps` L236-261, `ApplyBorderProps` L470-500 |
| M | `ApplyBorderProps` 中 `BrushColor` 分支为非 RoundedBox 无纹理 Border 加载 WhiteSquareTexture，但 `ApplyTypedBorderBrush` 的 typed 路径无对应逻辑，渲染结果不一致 | `ApplyTypedBorderBrush` L818-819 vs `ApplyBorderProps` L451-467 |
| L | `ApplyButtonProps` 中 Hovered 颜色用 `Color * 1.1f`，Alpha 也被乘以 1.1 可能超过 1.0 | `ApplyButtonProps` L178 |
| L | `GetPropsApplierMap` 精确类匹配 + 继承链遍历，扩展注册中间类时命中顺序可能不符预期 | `GetPropsApplierMap` L716-758 |
| L | `#define GET_PROP` 宏两次定义/销毁，名称过于通用，建议改用 inline lambda | 全文件 L49/L712/L825/L846 |

### #33 UMGSlotApplier (1M 2L)

| 级别 | 问题 | 位置 |
|------|------|------|
| M | `ApplyScrollBoxSlot` 与 `ApplyBoxSlotCommon` 存在重复的 Padding/HAlign/VAlign + SizeRule/Fill 逻辑，可复用模板 | `ApplyScrollBoxSlot` L107-120 vs `ApplyBoxSlotCommon` L44-57 |
| L | `ApplySlotProperties` 中 `bHasNonDefaultCanvas` 的 10 行浮点比较链可读性差，可提取为辅助方法 | `ApplySlotProperties` L211-221 |
| L | `#include "UMGWidgetUtils.h"` 作为主 include 应放第一行，文件名与头文件不对应 | 文件顶部 L3 |

### #34 UMGRenderUtils (2M 3L)

| 级别 | 问题 | 位置 |
|------|------|------|
| M | 文件名 `UMGRenderUtils.cpp` 与 `#include "UMGWidgetUtils.h"` 不匹配，无对应 `.h`，应统一命名 | 文件级 |
| M | `RenderWidgetToPng` 与 `RenderSlateWidgetToPngData` 大段重复代码（RT 创建、Renderer 设置、像素读取、PNG 编码），可直接复用 | 两函数对比 |
| L | `GetDesignerPreviewWindowSize` 已无调用方，是死代码，相关 include 也多余 | 匿名 namespace 第一个函数 |
| L | `ComputePreviewRenderSize` 连续调用两次 `SlatePrepass(1.0f)`，无注释说明原因 | L50-51 |
| L | `ComputePreviewRenderSize` 接收 `Widget` 参数但从未使用其成员，签名误导 | `ComputePreviewRenderSize` |

### #35 SDesign2UMGPreviewWindow (1M-资源 1L)

| 级别 | 问题 | 位置 |
|------|------|------|
| M资源 | `CreatePreviewWidgetFromJson` 中 `ResolveTransientTextures` 通过 `AddToRoot()` 阻止 GC，但 `TextureMap` 是局部变量，函数结束后引用丢失，从未调用 `ReleaseTransientTextures`，每次刷新预览泄漏 rooted 纹理 | `CreatePreviewWidgetFromJson` L568-569 |
| L | `AsyncTask(ENamedThreads::GameThread, ...)` 未 `#include "Async/Async.h"`，依赖间接包含，不稳定 | `RefreshUMGPreview` L478 |

### #36 DesignImportUtils (1H 1M 1L)

| 级别 | 问题 | 位置 |
|------|------|------|
| H | `GenerateBlueprintAndPng` 用原始 `OutputPath` 而非已解析的 `FinalPath` 提取 PNG 文件名，`OutputPath` 为空时文件名丢失，保存为 `.png` | `GenerateBlueprintAndPng` L118 |
| M | `FGenerateCallback` 类型别名已声明但从未使用，`OpenPreviewWindow` 无回调参数，属死代码 | `OpenPreviewWindow` / .h L34-43 |
| L | `GenerateBlueprintAndPng` 跳过增量更新和 `FImportResult` 汇总通知，与 `GenerateBlueprint` 行为不一致 | 两函数对比 |

### #37 Design2UMGNotification (1M 2L)

| 级别 | 问题 | 位置 |
|------|------|------|
| M API | `Success()` 中 `FAssetData(FSoftObjectPath(AssetPath).TryLoad())` 同步加载完整对象仅为构造 FAssetData，应用 AssetRegistry 直接获取 | `Success()` L20 |
| L | `Warning()` 使用 `CS_None` 完成状态，无视觉图标，与 Success/Error 不一致 | `Warning()` L38 |
| L资源 | `Success()` 超链接回调中 `TryLoad()` 同步加载，大资产时可能短暂卡顿 | `Success()` L20 lambda |

### #38 Design2UMGTests (2H 3M 1L)

| 级别 | 问题 | 位置 |
|------|------|------|
| H资源 | Test 12 硬编码绝对路径 `G:/UE5/FPSRoguelike/Designs/pencil-welcome.pen`，其他机器必定失败 | `FPenWelcomeBatchImportTest::RunTest` L819 |
| H资源 | Test 12 向 `/Game/Design2UMG/PencilWelcome` 写入真实资产，测试结束后无清理逻辑，反复运行污染 Content | `FPenWelcomeBatchImportTest::RunTest` L837 |
| M | Test 6 直接修改全局单例 Settings 字段，异常退出时恢复代码不执行，应用 RAII guard | `FIconFontNoMappingFallbackTest::RunTest` L348-384 |
| M | Test 3 对浮点值用 `TestEqual` 精确比较，同文件 Test 8.5 已用 `IsNearlyEqual`，未统一 | `FLayoutCalculatorTest::RunTest` L242-275 |
| M | `#include "PenImporter.h"` 和 `#include "DesignImportUtils.h"` 位于文件第 810-811 行而非顶部 | L810-811 |
| L | 测试编号不连续，存在 "Test 8.5"，后续未重新编号 | L610 注释 |

---

## H 级发现核实结论

> 22 条 H 级发现，经源码逐条核实：**16 条确认，6 条误报，1 条降级**

### ✅ 确认（16 条）

| # | 发现摘要 | 核实理由 |
|---|---------|---------|
| 4-H | ImportSingleFrame nullptr 四种含义混用 bOutOperationCanceled | 失败/取消/预览均走同一路径，编辑器误判为用户取消 |
| 5-H | BuildDocumentJson 接收 VarsObj 而非 DocObj | VarsObj 不含 version/variables/themes/fonts，全部丢失 |
| 8-H1 | DeepCopyNode 遗漏 Descendants 深拷贝 | TMap 值拷贝但 TSharedPtr 仍共享原始节点，会交叉污染 |
| 8-H2 | ApplyDescendantOverride 直接插入共享指针 | 多实例共享同一 Override 节点，修改互相影响 |
| 10-H1 | Convert 通过 TSharedPtr 绕过 const 语义 | 直接修改 X/Y/Width.Value/Height.Value，多次调用数据污染 |
| 10-H2→M | static TSet WarnedFamilies 非线程安全 | 降级 M：最坏只是重复打印警告，不崩溃 |
| 11-H1 | Angular 渐变 fallthrough 到 Radial 分支 | 两处代码都有，渲染结果完全不正确 |
| 11-H2 | 多 Shadow 效果固定 key 覆盖 | TMap::Add 覆盖旧值，只保留最后一个 Shadow |
| 18-H | SerializeSlot 未序列化 bAutoSize | Serialize 和 Parse 两端都缺失，往返丢失 |
| 23-H1 | FindMissingFonts 精确匹配 vs FindFontAssetPath 模糊匹配 | 同一字体两函数结论矛盾，误报缺失 |
| 23-H2 | DownloadToFile 主线程同步阻塞 30 秒 | 无 Tick/进度条，编辑器完全冻结 |
| 24-H | Rasterize Outer 参数从未使用 | API 签名误导，纹理始终在 TransientPackage |
| 28-H | FilterByAccepted 对 Removed 条目拒绝是空操作 | 新树无该 ID，用户"拒绝删除"被静默忽略 |
| 36-H | GenerateBlueprintAndPng 用 OutputPath 而非 FinalPath | 空 OutputPath 时 PNG 文件名丢失 |
| 38-H1 | Test 12 硬编码绝对路径 | 其他机器必定失败 |
| 38-H2 | Test 12 无资产清理逻辑 | 反复运行污染 Content 目录 |

### ❌ 误报（6 条）

| # | 发现摘要 | 误报原因 |
|---|---------|---------|
| 6-H | FindAvailablePort TOCTOU 竞态 | 端口探测标准做法，编辑器场景概率极低可重试 |
| 7-H | 变量循环引用无限递归 | ResolveThemedValue 直接返回字符串，不递归回 ResolveString |
| 14-H | FixEqualSpacing 硬编码 0-1 范围 | 调用前已 NormalizeFixerBounds，0-1 是正确设计 |
| 15-H | FBounds 像素 vs 归一化矛盾 | 函数期望归一化输入，与 Fixer 坐标系一致，注释有歧义但非 bug |
| 19-H | NextRequestId 数据竞争 | 两处递增都在 Game Thread，API 约定不支持并发 |
| 20-H | NodeIds 未 URL 编码冒号 | 冒号在 query string 合法（RFC 3986），Figma API 接受原样传递 |
