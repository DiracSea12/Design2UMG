# Design2UMG 文本与字体专项策略

最后核实：2026-05-19

这份文档只做一件事：把文本和字体专项的执行方向说清楚；正式交付以本文的可执行准则为准。

## 一句话结论

文本专项不要继续靠“字体名经验表”扩张。主线应该是：

1. 用户字体资产优先。
2. 字体实例和度量优先。
3. 运行时实测优先。
4. 按布局形态和布局约束求解。
5. 历史 family fallback 只做最后兜底，并且必须逐步压缩。

## 核心目标

### 1. 先解决字体身份问题

用户明确提供字体时，当前可执行含义不是“插件去下载或导入字体文件”，而是“真实 UE 项目里已经有可加载的 `UFont` 资产，并通过 Design2UMG 设置显式指定”。字体解析应优先使用这些项目资产，而不是先靠 family name、系统字体或环境 fallback 猜。

当前可用入口必须说清楚，不能只写“用户字体优先”：

- **显式字体映射入口**：`ExplicitFontMappings` 是当前主入口。用户把设计声明里的 `fontFamily` 或 `fontPostScriptName` 映射到项目内已有 `UFont` 资产，resolver 再按资产里的 typeface 解析字重和样式。
- **默认字体诊断入口**：`DefaultFontAsset` 是缺字体时写入诊断报告的项目级 fallback 候选。它应该指向项目内真实可加载、可渲染的 `UFont`，不能是空 `CompositeFont` 或没有有效字体数据的占位资产；当前生成路径不会因为缺字体就把它强行写进 `Widget.Text.FontFamily`。
- **源声明入口**：设计文档里的 `fontFamily / fontPostScriptName / fontWeight / fontStyle` 是字体身份线索和诊断输入，不是自动导入指令。`fonts[] name/url` 若未来重新作为字体文件输入，必须另开明确 phase；当前 Phase 31.4 不再把它当作下载或导入主路径。
- **物理字体文件入口**：如果用户只有 `.ttf/.otf/.ttc/.otc` 等物理字体文件，当前策略是先由用户或后续明确工具链把它们导入真实 UE 项目，形成 `UFont` 资产，再交给 `ExplicitFontMappings` 使用。本文不再承诺自动下载、自动导入、known web font catalog 或交互式上传 UI。

这里的来源优先级要分清：

- 显式映射命中的项目字体资产优先。
- 没有显式映射时，resolver 可以按当前项目内可用资产和声明信息尝试解析，但不能下载、导入或静默修复字体。
- 缺字体时，诊断报告记录 `DefaultFontAsset` 或 engine default 作为 fallback 候选，并说明已经降级；生成的 UMG 中间节点仍保持“未命中设计字体”的状态，不伪装成精确绑定。
- engine fallback 只能放在最后，并且也要作为降级状态看待。

如果要写成一句话，就是：
**先吃用户在项目里明确指定的字体资产，再吃当前项目内可解释的字体资产；缺失时在诊断里报告 DefaultFontAsset，最后才报告 engine fallback。**

插件真正要做的链路不是“看到字体名就完事”，而是：

1. **读取声明**：从设计节点读取 `fontFamily / fontPostScriptName / fontWeight / fontStyle / fontSize / lineHeight / letterSpacing`。Figma 通常只给声明，不给字体文件。Figma 还可能通过 `characterStyleOverrides / styleOverrideTable` 表达区间字体，不能只读节点级 `style` 后就声称拿到了完整字体身份。
2. **解析项目字体资产**：把声明和 `ExplicitFontMappings` 解析成 UE 可加载的 `UFont` / composite font 资产；`DefaultFontAsset` 只作为未命中时的诊断 fallback 候选记录。这里不做自动下载、自动导入或 known web font catalog 兜底。
3. **建立字体匹配证据**：把设计声明映射到 UE 资产和 typeface，例如 `Inter + 700 + normal -> /Game/Design2UMG/Fonts/Inter.Inter, Typeface=Bold`。匹配证据至少要记录 family、postscript name、style、weight、UE asset path、typeface entry name、resolution state 和 issue codes。
4. **写入 UMG 中间节点**：转换文本节点时，把 resolved asset path 写入 `Widget.Text.FontFamily`，把 resolved typeface 写入 `Widget.Text.TypefaceFontName`，字号继续走文本测量/校准链。富文本 run 也必须走同一套绑定，不能只处理普通 `TextBlock`。
5. **应用到真实控件**：构建 UE widget 时，`FontFamily=/Game/...` 必须被加载成 `FSlateFontInfo.FontObject`，`TypefaceFontName` 写入 `FSlateFontInfo.TypefaceFontName`，最后调用对应控件的 `SetFont()` 或等价样式应用路径。也就是说，最终证据不是“声明 Inter”，而是“这个 TextBlock 的 `FSlateFontInfo` 指向了哪份 `UFont` 和哪个 typeface”。

当前 Phase 31.4 已把自动字体导入链删除，并把主路径收口到 `FFontResolver`、显式映射、默认字体诊断候选和字体解析诊断。后续还缺的是：Figma 区间字体解析、每个文本节点的最终字体绑定证据、rich text run 的同等证据，以及字体度量/布局求解继续从 family-name 经验表迁到可解释的运行时观测。

这里不能偷懒成“只要 family name 对上就算命中”。项目字体资产不能只凭 family name 复用，必须有明确的显式映射、postscript name、typeface 或等价证据。若未来重新引入物理字体文件接入，也必须另开 phase 明确导入、去重和资产生命周期，不能把自动导入偷偷塞回当前主路径。

这一步要回答：

- 最终 UMG/Slate 实际命中的字体资产是谁。
- 命中的 typeface / weight / style 是否接近设计稿。
- 是否已经因为缺字体、缺字重、缺样式或缺 glyph 进入 fallback。

### 2. 再解决字体度量问题

即便项目字体资产命中了，设计工具和 UMG/Slate 的排版结果仍可能不同。后续专项应引入很薄的字体度量指纹，用来描述字体行为，而不是只记字体名字。

第一版指纹不需要大而全，而且要说清楚它是 Slate layout observation，不是完整 OpenType 真度量。公开 `FSlateFontMeasure` 能拿到的是 Slate 观测指标，不能声称已经拿到 `unitsPerEm / xHeight / capHeight` 这类字体物理字段。

优先覆盖：

- `GetMaxCharacterHeight()` 归一化结果。
- `GetBaseline()` 归一化结果，但必须保留符号，字段名建议用 `SignedDescenderRatio` 这一类清楚名字。
- 分脚本平均 advance 或小样本文本宽度。第一版主路径只能使用能证明命中目标字体/typeface 的原生样本；跨平台系统敏感的 fallback 脚本样本不能混进西文字体宽度平均值，也不能直接作为 fallback scale cache key。CJK / fallback 脚本指标可以作为后续显式脚本方案或 automation/debug 分布记录，不能在当前主路径里把操作系统字体栈污染写进 key。
- 度量是否可用、是否稳定。

OpenType 真度量、变量轴实例和更底层字体字段可以作为长期方向，不能混进本期可执行主路径里装作已经拿得到。

### 3. 最终以运行时实测和约束求解为准

字号和文本框结果不能只看源 JSON 的 `fontSize`。最终结果应由这些因素共同决定：

- 布局形态：单行标签、多行正文、ellipsis、icon font、固定宽/高约束等。
- box 约束：宽、高、最大行数、wrap、truncate。
- 行高、letter spacing、对齐方式。
- UMG/Slate 运行时测量结果。

## 明确禁止

下面这些做法不应再作为文本专项主路径：

- 继续扩 `FamilyFallbackScales` 这类字体名表。
- 按 case 名、节点 id、文件名、组件名、字体名写分支。
- 写 `if (FontFamily == ...)`、`Contains("mono")`、`Contains("code")` 这类名字判断来修分。
- 默认把 source `fontSize` 当成 UMG runtime font size。
- 默认启用动态测量 fallback scale，除非 full formal gate 证明零回退。
- 为了某几个样例分数好看，新增无法解释成通用规则的补丁。

## 推荐落地顺序

1. 先把文本路径按布局形态拆清楚，让每类文本有明确目标函数。允许使用宽高约束、wrap、truncate、最大行数、text growth、icon-font 语义等源数据；禁止为了修分去解析文本内容，例如数空格、数大小写、数数字、按字符串内容判断 token/prose。
2. 强化用户字体资产优先级，并记录实际命中的字体资产、命中来源、typeface / weight / style 和 fallback 状态。这是后续度量指纹和求解器的地基，不是可选优化。
3. 引入字体度量指纹，把 cache 和校准从“名字驱动”逐步迁到“布局行为驱动”。如果第 2 步还不能提供结构化字体身份证据，任何度量指纹或 solver phase 都必须先标为 blocked，不能越过地基硬做。
4. 把 runtime probe + autosize solver 提升为主证据。
5. 把 `FamilyFallbackScales` 压缩成最后兜底。主路径应先使用 runtime probe 按 line-box / width 约束求出 measured scale；probe 或度量拿不到时先使用通用 `DefaultFontFallbackScale`。只有无法取得 metric/probe 证据时，才允许最后命中历史 family fallback。focused/formal 回退不能当成“测量不稳定”的证据。
6. 每轮 formal gate 后统计历史 fallback 的真实命中价值，再决定能不能继续收缩或删除。

## 文档和代码变更必须说明的事

任何文本和字体专项变更，至少要在方案、任务或 closeout 中说明：

- 这次改动处理的是字体身份问题、字体度量问题、布局求解问题，还是验证污染问题。
- 用户字体资产的优先级有没有变化。
- `FamilyFallbackScales` 或类似历史兜底是否被新增、扩大、压缩或删除。
- cache key 是否仍以 family/typeface 为主，还是已经开始纳入度量和布局行为。
- cache 修改的是哪类 key。当前已有 `ResolvedTextScaleByCalibrationKey`、`SharedStyleMinScaleByStyleKey`、`BaselineFallbackScaleByProbeKey`，不能继续写一个不存在的泛泛 “Bucket Cache”。精确 `FVector2D` / actual measurement cache 不得和 fallback scale key 混用。
- fallback scale cache key 里的宽高约束、字号、letter spacing、line-height 等浮点输入是否做了稳定归一化或有限精度量化。原始 float 直接进 key 会把命中率打烂；过粗量化又会误复用，两边都不能糊弄。
- focused 测试覆盖了什么，full formal gate 是否证明没有 case 退步。

## 验收口径

focused text automation 只能证明局部行为没有明显坏掉，不能单独证明文本专项可放行。

字体专项的评分必须拆成两套读数：

- 字体/文本通道：只看文本和字体拥有的区域与指标，例如 glyph raster、字号、line-height、letter spacing、文本框内换行/截断、icon-font 文字渲染。
- 非字体通道：看文本区域之外的布局、图形、图片、填充、描边、圆角、阴影、层级、尺寸和结构。

改字体时，非字体通道不能回退。字体通道可以有单独的专项回退标准，但这个标准必须写进对应 phase 的 spec / closeout，并且必须是机器可读报告能验证的标准，不能靠人工一句“这是字体问题”把整图退步洗掉。

当前验证工具如果还只输出整图 `summary.no_regression` 和 `validation_against_historical_best.json`，就没有双通道豁免。此时仍按整图零退步判定，直到 formal report 或 companion validation report 明确产出字体通道与非字体通道结果。

可放行的最低证据是：

- 真实项目验证，不使用 HostProject。
- full 47-case formal gate。
- `summary.no_regression` 没有退步。
- `validation_against_historical_best.json` 没有落后于当前可追溯历史最佳。
- 如果使用字体专项独立标准，必须同时证明非字体通道相对 historical best 零退步。
- 没有通过字体名、样例名、节点 id 或路径特判来换分。
