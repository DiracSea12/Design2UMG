# Design2UMG 已证伪方向记录（2026-03-19）

## 范围

本文件只记录 2026-03-19 这轮自动化验收中，已经被本地回归结果证伪，或已确认“当前没有收益”的优化方向。

本文件不记录主观猜测，只记录有本地报告、日志或回归目录支撑的结论。

## 当前基线

- 当前最佳稳定状态对应全量报告目录：`Saved/Design2UMG/Automation/pen_full_truth_batch_20260319_latest_v4`
- 当前最佳稳定状态总分：`7/15` 通过
- 当前保留且已证明有效的改动只有一项：单行文本 `fit_content` 高度按 design-space px 计算
- 当前 15 个 case 的 `generation_vs_asset_changed_ratio` 全部为 `0.0`
- 结论：当前主问题不在“生成态 WidgetTree”和“资产重载态编译 UMG”之间的偏差

## 历史已证伪方向（来自旧文档，现统一汇总）

以下条目不是本轮新发现，而是此前已经在仓库文档中被明确判定为“不成立”或“非 bug”，这里只做统一归档，避免后续重复进入同一死路。

来源文档：

- `Docs/archive/legacy-plans/FullPipelineReview.md`
- `Docs/LessonsLearned.md`

### H-1. “ParsePadding 的 2 元素数组应该按 CSS 语义解释”

- 结论：已证伪
- 旧文档结论：
- `[0]=左右 [1]=上下` 是 pen 格式约定，不是 CSS 语义
- 因此这不是解析 bug
- 原始记录：
- `Docs/archive/legacy-plans/FullPipelineReview.md`

### H-2. “Ref 展开不递归，所以嵌套 Ref 本身就是 bug”

- 结论：已证伪
- 旧文档结论：
- `ConvertNode` 递归调用时每层都会展开 Ref
- 真正的问题是某一轮里“展开后的结果没被调用方正确使用”，不是“Ref 天然不递归”
- 原始记录：
- `Docs/archive/legacy-plans/FullPipelineReview.md`

### H-3. “元数据节点会错误生成 CanvasPanel，是核心问题”

- 结论：已证伪
- 旧文档结论：
- 元数据/设计器专用节点已经由 `ShouldSkipNode` / `IsDesignerOnlyNodeType` 过滤
- 不会真正走到 `MapNodeTypeToWidget`
- 原始记录：
- `Docs/archive/legacy-plans/FullPipelineReview.md`

### H-4. “LayoutFixer 使用 [0,1] 边界检查是错的”

- 结论：已证伪
- 旧文档结论：
- `LayoutFixer` 使用的是全局归一化 Bounds
- `[0,1]` 正是画布范围，不是魔法常量误用
- 原始记录：
- `Docs/archive/legacy-plans/FullPipelineReview.md`

### H-5. “Stretch 模式下 OffsetEnd 的符号反了”

- 结论：已证伪
- 旧文档结论：
- `(1.0f - End) * CanvasSize` 是右边距像素值，公式本身正确
- 原始记录：
- `Docs/archive/legacy-plans/FullPipelineReview.md`

### H-6. “WrapWithMultiFill 与 ApplyFillProperties 对单层 fill 的处理不一致”

- 结论：已证伪
- 旧文档结论：
- 两处都按 `Fills.Num() > 1` 处理，多层/单层判断是一致的
- 不是当时的主根因
- 原始记录：
- `Docs/archive/legacy-plans/FullPipelineReview.md`

### H-7. “Gap 和 JustifyContent 共存本身就是语义冲突”

- 结论：已证伪
- 旧文档结论：
- `Gap` 体现在子节点 `Slot.Padding`
- `JustifyContent` 体现在插入新的 `Spacer`
- 两者职责不同，不构成逻辑冲突
- 原始记录：
- `Docs/archive/legacy-plans/FullPipelineReview.md`

### H-8. “当前最佳修法是继续围绕早期 Canvas FitContent 五连修打补丁”

- 结论：已证伪为当前主线
- 说明：
- `LessonsLearned.md` 里那批修复在当时确实把大面积零尺寸问题从 `81.6%` 拉回来了
- 但它们解决的是早期“Canvas FitContent 塌缩为零尺寸”的问题
- 不是今天这轮 `7/15` 卡住的主问题
- 当前如果继续把这批旧修法当主线反复微调，已经没有收益证据
- 原始记录：
- `Docs/LessonsLearned.md`

## 已证伪方向

### 1. “生成态和 asset_reload 不一致，所以分数不可信”

- 结论：已证伪
- 证据：
- `pen_full_truth_batch_20260319_latest_v4/summary.json`
- 15 个 case 的 `generation_vs_asset_changed_ratio` 全部为 `0.0`
- 含义：当前分数反映的是编译后资产渲染图和参考图的差异，不是 bootstrap 图和 asset 图不一致导致的误判

### 2. “welcome_step-2-frame 的根 fit_content 链本身坏了”

- 结论：已证伪为主根因
- 初始症状：
- 旧结果中 `welcome_step-2-frame` 的 alpha bbox 为 `(0,16,2880,520)`，上下各缺 `16px`
- 旧结果分数：
- `changed_ratio = 0.13148450456053068`
- `mae = 0.04215162443501447`
- 证据：
- `Saved/Design2UMG/Automation/pen_full_truth_batch_20260319_latest_v3/pen_pencil-welcome_step-2-frame/compare_report.json`
- 后续结果：
- 修正单行文本高度后，`welcome_step-2-frame` 降到
- `changed_ratio = 0.07396351575456053`
- `mae = 0.022071547135645304`
- 且 alpha bbox 已回到和参考图同尺寸覆盖
- 证据：
- `Saved/Design2UMG/Automation/_debug_text_measure_2/summary.json`
- `Saved/Design2UMG/Automation/pen_full_truth_batch_20260319_latest_v4/pen_pencil-welcome_step-2-frame/compare_report.json`
- 含义：`step2` 的根高度异常主要是内部文本/行高估算偏低传导出来的症状，不是根节点 fit_content 主逻辑整体失效

### 3. “把单行文本统一切到 Slate STextBlock 实测高度就能解决问题”

- 结论：已证伪
- 做法：
- 把单行文本也强制走 `STextBlock::ComputeDesiredSize` 路线
- 结果：
- `welcome_step-2-frame` 直接恶化到
- `changed_ratio = 0.49637683975953567`
- `mae = 0.1544977909929519`
- 实际表现：
- 页面整体高度被撑坏，候选图 alpha bbox 只剩 `(0,0,2880,292)`
- 证据：
- `Saved/Design2UMG/Automation/_debug_text_measure_1/summary.json`
- 结论补充：
- “单行文本高度要修”是对的
- “直接全面切到 Slate 实测高度”是错的

### 4. “只要保留 Border 的 DrawAs，不让 BrushColor 覆盖成 Image，就能修回缺失边框”

- 结论：当前已证伪为无收益
- 做法：
- 在 `UMGPropertyApplier.cpp` 中仅当 `DrawAs == NoDrawType` 时才把 Brush 改成 `Image`
- 结果：
- `welcome_step-2-frame` 和 `welcome_step-1-frame` 分数与 `_debug_text_measure_2` 完全一致
- 没有任何可测改善
- 证据：
- `_debug_text_measure_2/summary.json`
- `_debug_border_fix_1/summary.json`

### 5. “把普通 stroke 默认从 Box 改成 RoundedBox，可以修回表格/组件边框”

- 结论：已证伪
- 结果：
- `welcome_step-2-frame` 从 `0.0739635 / 0.0220715` 恶化到 `0.1130364 / 0.0237917`
- `welcome_step-1-frame` 也变差
- 证据：
- `_debug_text_measure_2/summary.json`
- `_debug_border_fix_2/summary.json`
- 含义：
- 这条改动没有解决单边描边问题，反而引入了新的外框偏差

### 6. “非均匀 stroke 可以用 Overlay/Canvas 手工补四边细线快速修好”

- 结论：已证伪为当前无收益方向
- 尝试过的版本：
- `Overlay + Border`
- `CanvasPanel + Border`
- `CanvasPanel + Image`
- 调整子节点顺序
- 结果：
- `welcome_step-2-frame` 长时间停在大约 `0.0931 / 0.02337`
- 明显差于当前最佳稳定状态 `0.0739635 / 0.0220715`
- 证据：
- `_debug_side_stroke_1/summary.json`
- `_debug_side_stroke_2/summary.json`
- `_debug_side_stroke_3/summary.json`
- 额外事实：
- 运行时日志已经确认这条分支确实命中
- `headerRow` 和 `row1..row4` 的 bounds 与 `bottom=1` 都是正确的
- 但最终像素结果没有把横向分隔线画出来
- 说明问题不在“分支没走到”，而在更底层的 UE 渲染/控件表达能力
- 证据：
- `_debug_side_stroke_log_1/unreal_batch.log`

### 7. “非均匀 stroke 改走 PathGeometry 栅格化矩形线条，就能作为当前主修补方案”

- 结论：已证伪为当前主线方案
- 结果：
- `welcome_step-2-frame` 仍失败
- `changed_ratio = 0.08874896351575456`
- `mae = 0.02333518655700257`
- 并伴随 `editor_exit_code = 3221225477`
- 证据：
- `_debug_path_stroke_1/summary.json`
- 含义：
- 这条路即使略优于某些 side-stroke 试验，也没有优于当前最佳稳定状态，而且会引入编辑器崩溃风险

### 8. “DrawAs=Box 的 OutlineSettings 能真正画出边框，所以当前 stroke 语义本身没问题”

- 结论：已证伪
- 证据来源：
- Unreal Engine 本地源码 `Runtime/SlateCore/Public/Styling/SlateBrush.h`
- 引擎注释明确写明：
- `OutlineSettings` 当前只用于 `RoundedBox`
- 当前实际观测：
- `step2` 编译态里，把 Border 从 `Image` 保回 `Box` 后，分数几乎不变
- `welcome_step-2-frame` 仍然停在约 `0.0739000 / 0.0220515`
- 证据目录：
- `Saved/Design2UMG/Automation/_debug_step2_verbose_2`
- 含义：
- 不能再把 “Box + OutlineThickness/OutlineColor” 当成真实可渲染的边框方案继续微调
- 这不是参数问题，而是引擎能力边界

### 9. “只修 Brush 被降成 Image 这一层，step2 的分隔线就会自动恢复”

- 结论：已证伪为充分条件
- 做法：
- 在 `UMGPropertyApplier.cpp` 中保留已有 `DrawAs`，给缺资源的 Border 注入白纹理时不再强制改成 `Image`
- 运行时事实：
- 编译态 Border 确实从 `drawAs=Image` 变成了 `drawAs=Box`
- 但 `step2` 分数基本不变
- 证据目录：
- `Saved/Design2UMG/Automation/_debug_step2_verbose_1`
- `Saved/Design2UMG/Automation/_debug_step2_verbose_2`
- 含义：
- 这条修复能提升“编译态语义完整性”，但它本身不是当前像素误差的主解
- 根因仍然是 `Box` 本身不画 outline

### 10. “显式 side-stroke 线条 Overlay/Image 包装已经可以作为当前主线”

- 结论：已证伪为当前主线方案
- 做法：
- 用 `Overlay + Image(WhiteSquareTexture)` 显式生成 bottom/right 线条
- 结果：
- `welcome_step-2-frame` 恶化到
- `changed_ratio = 0.09132721807628524`
- `mae = 0.023201283208142295`
- 证据目录：
- `Saved/Design2UMG/Automation/_debug_step2_overlay_1`
- `Saved/Design2UMG/Automation/_debug_step2_overlay_2`
- 补充事实：
- 这条路不是“完全没画出来”
- 实际上 header 处分隔线能画出来，但会引入额外布局漂移和位置错层
- 含义：
- 说明“显式线条”方向不是零信息，但当前这版 Overlay 实现不能保留为主干代码

### 11. “显式 side-stroke 改成 CanvasPanel 绝对定位后就能稳定解决布局漂移”

- 结论：已证伪
- 做法：
- 把 side-stroke 包装从 `Overlay` 改成 `CanvasPanel`，尝试让线条不参与测量
- 结果：
- `welcome_step-2-frame` 进一步恶化到
- `changed_ratio = 0.10005895004145937`
- `mae = 0.023903854400383704`
- 证据目录：
- `Saved/Design2UMG/Automation/_debug_step2_canvas_1`
- 含义：
- 当前 CanvasPanel 绝对定位版比 Overlay 版更差，不能继续沿用

### 12. “按某个 case 的官方导图去全局改 placeholder surface 默认配色，可以作为通用修法”

- 结论：已证伪
- 做法：
- 先从 `welcome_step-1-frame` 的真实 `TableCell` 实例导图反推出 placeholder 色值
- 把全局默认 placeholder 配色从 `#D282FF33 / #D480FF` 改成更贴近该实例的值
- 单例结果：
- `welcome_step-1-frame` 从 `0.2085752 / 0.0159059` 改善到 `0.1993438 / 0.0167060`
- 但全局回归结果：
- `pen_pencil-welcome_lunaris-design-system-components` 直接恶化到 `0.0853910 / 0.0062151`
- 证据目录：
- `Saved/Design2UMG/Automation/_debug_step1_no_default_stroke_fill_1`
- `Saved/Design2UMG/Automation/_debug_step1_placeholder_color_1`
- `Saved/Design2UMG/Automation/_debug_welcome_focus_round3_1`
- 含义：
- 该改动本质上是在拟合单个设计软件/单个页面实例的像素表现
- 不具备跨页面、跨设计稿的泛化性，不能保留

### 13. “把 flow 布局下容器级 fit_content 全部改成 MinDesired 语义，可以作为通用修法”

- 结论：已证伪
- 做法：
- 把 `LayoutMapper.cpp` 里的 `ShouldUseFitOverride()` 改成只对叶子节点返回 `true`
- 让带子节点的 `fit_content` 容器统一走 `MinDesiredWidth/MinDesiredHeight`
- 预期：
- 希望 badge/tab/chip 之类组件不要被预估尺寸硬钉死，允许运行时内容轻微放大
- 实际结果：
- `welcome_step-2-frame` 从本轮已改善状态
- `0.0391467 / 0.0147954`
- 直接恶化到
- `0.0837978 / 0.0225862`
- 且第一列 status badge 明显下沉
- 证据目录：
- `Saved/Design2UMG/Automation/_debug_structural_round_text_placeholder_1`
- `Saved/Design2UMG/Automation/_debug_structural_round_text_placeholder_2`
- 含义：
- 容器级 `fit_content` 在当前 UMG 生成链里不能直接放宽成 `MinDesired` 语义
- 这会破坏现有 badge/table 子树的垂直定位，不能保留

### 14. “纯布局容器的 padding 一律包成透明 Border，就能作为通用修法”

- 结论：已证伪
- 做法：
- 在 `Design2UMGConverter.cpp` 里把所有 `layout != none` 且带 `padding` 的容器
- 一律改成走 `WrapWithBorder()`
- 试图用透明 `Border` 的内容内边距去承接 `frame/group` 的布局 padding
- 预期：
- 修正 `Content` / `DataTable` 这类纯布局容器的 padding 没有真正落到 UMG 的问题
- 实际结果：
- `pen_pencil-welcome_step-1-frame`
- 从本轮最佳
- `changed_ratio = 0.09919116512345678`
- `mae = 0.011258807265190027`
- 直接恶化到
- `changed_ratio = 0.1612505787037037`
- `mae = 0.013680873653473737`
- 证据目录：
- `Saved/Design2UMG/Automation/_debug_step1_lineheight_effectfix`
- `Saved/Design2UMG/Automation/_debug_step1_layoutpadding`
- 含义：
- “布局 padding 没生效”这个症状是真问题
- 但“所有纯布局容器统一包透明 Border”这条实现会大范围改变层级和测量结果
- 不能保留

## 当前保留的有效改动

### 单行文本 design-space 高度修正

- 结论：保留
- 文件：
- `Source/Design2UMGEditor/Private/Design2UMGConverter.cpp`
- 核心点：
- `EstimateSingleLineTextHeight()` 不再使用已经缩放后的 UMG font size
- 改为使用 design-space `Node->FontSize`
- 已验证收益：
- `welcome_step-2-frame` 从 `0.1314845 / 0.0421516` 降到 `0.0739635 / 0.0220715`
- 该改动没有破坏已通过的 `dashboard-revenue`

## 暂时不要再重试的方向

- 不要再把单行文本全面切回 `STextBlock` 实测高度
- 不要再尝试“BrushColor 不覆盖 DrawAs”这一条单独改动
- 不要再尝试“普通描边一律 RoundedBox”
- 不要再继续在当前 `Overlay/Canvas` 单边描边补线实现上微调顺序、对齐或尺寸
- 不要再把 `PathGeometry` 矩形线条作为当前主修方案推进



