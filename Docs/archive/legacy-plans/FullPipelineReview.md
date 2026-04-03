# Design2UMG 全链路 Review 报告

14个管线节点，每节点一个独立agent审查。严重程度：🔴严重 🟡中等 🟢建议

基础路径：`Engine\Plugins\Marketplace\Design2UMG\Source\Design2UMGEditor\`

标注说明：✅已修复 ❌不成立 ⏸暂缓 🔽降级

---

## 1. PenParser (`Private\PenParser.cpp`)

- 🔴 ✅已修复 Fill双重解析：Fills数组未检查bEnabled，禁用层仍被处理 → 加了`&& F.bEnabled`检查（两处）
- 🔴 ❌不成立 ParsePadding 2元素数组语义 → [0]=左右 [1]=上下 是pen格式约定，非CSS，无需改
- 🟡 非容器节点Layout默认值为Horizontal，不合理
- 🟡 Descendants哨兵值方式脆弱
- 🟡 ParseSize对字符串"0"处理不对称
- 🟡 FPenVariableResolver按值拷贝整个Variables map
- 🟢 Fill解析重复代码可提取
- 🟢 部分字段缺UPROPERTY
- 🟢 FPenFrameInfo成员未初始化
- 🟢 ExtractTopFrames位置不当

## 2. FigmaParser (`Private\FigmaParser.cpp`)

- 🔴 🔽降级为🟡 Layout默认值与PenParser不对称 → 两条路径输入格式不同，默认值不同合理
- 🔴 ✅已修复 INSTANCE节点走Ref展开丢弃Figma已展开的children → MapNodeType返回Frame而非Ref，Figma API已展开children直接当Frame处理
- 🔴 🔽降级为🟡 Document元数据未填充 → Figma路径不依赖这些字段
- 🟡 ✅已修复 Shadow opacity未乘入颜色 → ParseEffects加ShadowOpacity参数传给ColorToHex
- 🟡 Stroke只处理SOLID类型
- 🟡 ✅已修复 缺少textAutoResize → TextGrowth映射 → ParseTextProperties末尾加textAutoResize解析
- 🟡 ✅已修复 缺少bFlipX/bFlipY解析 → ParseNode加relativeTransform矩阵负scale检测
- 🟡 缺少bPlaceholder解析
- 🟢 缺少layoutWrap
- 🟢 缺少blendMode
- 🟢 GetBool默认值问题
- 🟢 FigJam节点类型未处理

## 3. PenMCPBridge (`Private\PenMCPBridge.cpp`)

- 🔴 🔽降级为🟡 ConvertFromParsedDoc声称优化但逻辑相同 → 代码冗余但不影响功能
- 🟡 Reusable节点收集范围过广
- 🟡 Version读取逻辑冗余
- 🟡 Version来源从VarsObj取不合逻辑
- 🟢 去重逻辑可简化
- 🟢 缺少Design2UMGEDITOR_API导出宏

## 4. Design2UMGConverter (`Private\Design2UMGConverter.cpp`)

- 🔴 ✅已修复 第155行：Ref展开后传原始Child而非EffectiveChild → 改为传EffectiveChild
- 🟡 ✅已修复 Rectangle类型在switch中缺少显式case → 加了显式case
- 🟡 BoxShadow触发依赖Properties中的magic string
- 🟡 BackgroundBlur只取第一个匹配Effect，忽略后续
- 🟡 Convert()直接修改输入Document的子节点坐标（副作用）
- 🟢 Ellipse圆角只处理Fixed尺寸
- 🟢 IconFont的FontWeight未映射
- 🟢 TextAlign的Justify值未处理

## 5. PenRefExpander (`Private\PenRefExpander.cpp`)

- 🔴 ❌不成立 嵌套Ref不递归展开 → ConvertNode递归调用时每层都会展开Ref，P0-1修复后自然解决
- 🔴 ❌不成立 调用方展开Ref后未使用结果 → 与#4同一问题，已在#4修复
- 🟡 DeepCopyNode不深拷贝Descendants map，覆盖属性共享引用
- 🟡 Override检测用默认值做哨兵，合法默认值被误判为"未覆盖"
- 🟡 Object替换模式直接引用Override指针未深拷贝
- 🟢 ReThemeNode日志应用Verbose级别
- 🟢 Children override是浅拷贝

## 6. PenTypeMapper (`Private\PenTypeMapper.cpp`)

- 🔴 ❌不成立 元数据节点类型错误生成CanvasPanel → ShouldSkipNode/IsDesignerOnlyNodeType已过滤，不会走到MapNodeTypeToWidget
- 🟡 ParseColor是纯转发函数，冗余
- 🟡 ✅已修复 Font weight匹配区分大小写 → MapFontWeight开头加ToLower()
- 🟢 Text/IconFont的case可合并
- 🟢 Stretch alignment值未预留

## 7. PenLayoutMapper (`Private\PenLayoutMapper.cpp`)

- 🔴 ✅已修复 SpaceAround的FillWeight错误 → 首尾Spacer改为FillWeight=0.5
- 🔴 ❌不成立 Gap与JustifyContent共存时语义冲突 → Gap设置子节点Slot.Padding，JustifyContent插入新Spacer节点，两者不冲突
- 🟡 ApplySlotProperties默认对齐"Left"有隐式耦合
- 🟡 Gap用Slot.Padding vs ApplyPadding用Properties Padding，命名混淆
- 🟡 ✅已修复 WrapWithSizeBox将float截断为int，亚像素精度丢失 → 改为SanitizeFloat
- 🟢 HasPadding不处理负值
- 🟢 Fill检测用字符串比较

## 8. PenVisualMapper (`Private\PenVisualMapper.cpp`)

- 🔴 ✅已修复 WrapWithBoxShadow中OffYStr空指针 → 加了`!OffYStr`检查
- 🔴 ❌不成立 WrapWithMultiFill与ApplyFillProperties对单层fill的判断条件不一致，边界情况填充层丢失 → 两处都用`Fills.Num() > 1`，一致
- 🟡 Gradient/Image fill逻辑在单层和多层路径中重复（DRY违反）
- 🟡 BuildGradientMaterialParams只取首尾色标，多色标渐变退化为双色
- 🟡 ✅已修复 非均匀圆角的shadow使用0 → 非均匀时取四角平均值
- 🟡 多个Shadow effect只应用最后一个
- 🟡 Inner Shadow静默当作outer shadow处理
- 🟢 属性迁移硬编码字符串数组
- 🟢 Angular gradient复用radial gradient的MaterialRef
- 🟢 WrapWithBorder的Slot重置不一致

## 9. LayoutFixer (`Private\LayoutFixer.cpp`)

- 🔴 ❌不成立 FixEqualSpacing边界检查硬编码[0,1] → Bounds是全局归一化坐标，[0,1]就是画布范围，正确
- 🔴 🔽降级为🟡 MaxMove截断导致误差累积 → 有意的保守策略，非bug
- 🟡 FixChildrenSpacing用OriginalBounds做参考但自身用ProposedBounds
- 🟡 DetectRows/DetectColumns只与组内第一个元素比较
- 🟡 GetMedian按值传递不必要拷贝
- 🟢 AvgGap计算了但MaxDiff基于MedianGap
- 🟢 预筛选与内部逻辑重复
- 🟢 Rollback不写日志

## 10. LayoutCalculator (`Private\LayoutCalculator.cpp`)

- 🔴 ✅已修复 单元测试传像素值但CalculateSlot期望归一化值 → 测试改为归一化值（除以DesignSize）
- 🔴 ❌不成立 Stretch模式OffsetEnd符号可能反转 → `(1.0f - End) * CanvasSize`是右边距像素值，正确
- 🟡 bEnableNineGrid=false时锚点硬编码为0.0
- 🟡 Hysteresis只支持3个锚点
- 🟡 FBounds注释说"像素"但LayoutCalculator期望归一化
- 🟢 ShouldSnapToEdge参数命名不一致
- 🟢 GetStandardHUDPreset冗余

## 11. UMGWidgetUtils + Appliers (`Private\UMGWidgetUtils.cpp`, `Private\UMGPropertyApplier.cpp`, `Private\UMGSlotApplier.cpp`)

- 🔴 ✅已修复 DynamicClassCache静态TMap悬空指针 → 改为TWeakObjectPtr，查找时验证有效性
- 🔴 ✅已修复 ApplyImageProps和HandleImageTextureRef竞争Brush → HandleImageTextureRef现在保留ApplyImageProps设置的ImageSize
- 🔴 🔽降级为🟡 ApplyBorderProps与ApplyTypedBorderBrush双重设置 → typed设置Brush对象属性，legacy SetBrushColor设置tint，不同层面不冲突
- 🟡 TexturePath处理在ApplyImageProps和HandleImageTextureRef中重复
- 🟡 ApplyBorderProps的OutlineCornerRadius/CornerRadii与ApplyTypedBorderBrush重复
- 🟡 ImageMode Crop变换解析在Image和Border中重复
- 🟡 HasValidSlotHistory硬编码默认值
- 🟡 ParseMargin不支持2值简写
- 🟡 Widget命名去重上限1000
- 🟢 GET_PROP宏可改内联函数
- 🟢 ApplyBoxSlotCommon的Auto处理
- 🟢 ScrollBoxSlot/BoxSlotCommon的SizeRule重复
- 🟢 SynchronizeProperties时序问题

## 12. UMGGenerator (`Private\UMGGenerator.cpp`)

- 🔴 ✅已修复 资产命名do-while循环off-by-one → 循环条件改为`Suffix <= MaxAttempts`，退出后再验证
- 🟡 ✅已修复 CastChecked后又做null检查，逻辑矛盾 → CastChecked改为Cast
- 🟡 FixedRoot对大树按值拷贝性能差
- 🟢 Compile结果未检查
- 🟢 SavePackage前MarkPackageDirty冗余

## 13. PenRemoteImageImporter (`Private\PenRemoteImageImporter.cpp`)

- 🔴 ❌不成立 ResolveNodeRecursive只读Properties → 361行已有`Node.Brush.TexturePath = *TexturePath`回写逻辑
- 🟡 NormalizeUrl做了不必要的scheme重建
- 🟡 同步HTTP阻塞主线程（Flush）
- 🟡 ✅已修复 同一URL在树中可能被重复下载 → FResolveContext加UrlToAssetPath缓存
- 🟢 AddReportMessage 32条消息上限
- 🟢 两个匿名namespace

## 14. ProceduralShapeGenerator + PathRasterizer (`Private\ProceduralShapeGenerator.cpp`, `Private\PenPathRasterizer.cpp`)

- 🔴 ✅已修复 GenerateFromMaterialRef缺少ellipse和polygon匹配 → ellipse映射到circle，polygon映射到roundedrect
- 🔴 ✅已修复 Nonzero winding fill算法有bug且O(n³) → 改为标准O(n)算法：跟踪SpanStart在winding 0→非0时记录，非0→0时填充
- 🟡 Ring Thickness默认值.h和.cpp不一致
- 🟡 Thickness归一化阈值含义模糊
- 🟡 Scanline fill无抗锯齿
- 🟡 隐式lineto不处理相对坐标
- 🟢 纹理创建代码重复
- 🟢 Bezier固定段数

---

## 修复统计

### 🔴严重问题（22个）

| 验证结论 | 数量 |
|---------|------|
| ✅已修复 | 11 |
| ❌不成立 | 8 |
| 🔽降级为🟡 | 4 |
| ⏸暂缓 | 0 |

### 🟡中等问题已修复（10个）

1. FigmaParser Shadow opacity未乘入颜色 → `FigmaParser.cpp` ParseEffects加ShadowOpacity
2. FigmaParser 缺textAutoResize→TextGrowth映射 → `FigmaParser.cpp` ParseTextProperties末尾加解析
3. FigmaParser 缺bFlipX/bFlipY解析 → `FigmaParser.cpp` ParseNode加relativeTransform负scale检测
4. PenTypeMapper FontWeight大小写敏感 → `PenTypeMapper.cpp` MapFontWeight加ToLower()
5. PenLayoutMapper WrapWithSizeBox float截断 → `PenLayoutMapper.cpp` 改为SanitizeFloat
6. PenVisualMapper AddTextureSizeHints float截断 → `PenVisualMapper.cpp` 改为SanitizeFloat
7. PenVisualMapper 非均匀圆角shadow使用0 → `PenVisualMapper.cpp` 取四角平均值
8. UMGGenerator CastChecked后null检查矛盾 → `UMGGenerator.cpp` 改为Cast
9. Design2UMGConverter Rectangle缺显式case → `Design2UMGConverter.cpp` 加显式case
10. PenRemoteImageImporter 同一URL重复下载 → `PenRemoteImageImporter.cpp` 加UrlToAssetPath缓存

### 🔴已修复的11个问题

1. PenParser Fills数组未检查bEnabled → `PenParser.cpp` 两处加`&& F.bEnabled`
2. Design2UMGConverter Ref展开结果未使用 → `Design2UMGConverter.cpp:155` Child改为EffectiveChild
3. PenLayoutMapper SpaceAround权重错误 → `PenLayoutMapper.cpp` 首尾Spacer FillWeight=0.5
4. PenVisualMapper BoxShadow空指针 → `PenVisualMapper.cpp` 加OffYStr null检查
5. LayoutCalculator单元测试传像素值 → `Design2UMGTests.cpp` 改为归一化值
6. DynamicClassCache悬空指针 → `UMGWidgetUtils.cpp` 改为TWeakObjectPtr
7. Image Brush竞争覆盖 → `UMGWidgetUtils.cpp` HandleImageTextureRef保留设计稿ImageSize
8. ProceduralShapeGenerator缺ellipse/polygon → `ProceduralShapeGenerator.cpp` 加ellipse/polygon匹配
9. UMGGenerator off-by-one → `UMGGenerator.cpp` 循环条件修正
10. FigmaParser INSTANCE走Ref展开丢弃已展开children → `FigmaParser.cpp` MapNodeType返回Frame
11. PathRasterizer Nonzero winding算法bug且O(n³) → `PenPathRasterizer.cpp` 改为标准O(n)算法
