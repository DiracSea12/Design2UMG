# Design2UMG 文本测量调研与建议方案

最后核实：2026-05-19

这份文档是 Phase 31.4 字体收口后的背景材料，只记录后续文本专项可以参考的方向，不作为本轮实现范围或验收标准。

这份文档只回答两件事：

1. 外部已有研究和项目实践，对“字体很多、文本难测”这件事有什么靠谱经验。
2. 结合 Design2UMG 当前代码，后续更适合往什么方向演进。

说明：

- 这份文档只讨论文本问题。
- 这条线和当前 Phase 31.4 实现范围分开处理；Phase 31.4 只负责删自动字体导入链、改显式项目字体映射、补诊断和验证证据。
- 这里优先看“更通用、更不容易过拟合”的方法，不看样例特判。
- 这份文档是研究笔记和方向说明，不是开工执行规范。正式执行准则见 `Docs/TextFontStrategy.zh-CN.md`。

## 当前采用的执行方向

后续文本专项按 `Docs/TextFontStrategy.zh-CN.md` 执行。这里先把核心决策压缩成五句话：

1. 用户在真实 UE 项目里显式提供字体资产时，项目字体资产优先于 family-name 推断和环境 fallback。
2. 字体度量、字体实例和运行时实测结果优先于字体名字。
3. source `fontSize` 不是 UMG runtime font size 的真值，只能作为求解输入之一。
4. `FamilyFallbackScales` 只能逐步降为最后兜底，不能继续作为主路径扩表。
5. 所有文本和字体改动都必须用 full 71-case formal gate 证明没有对历史最佳造成单点退步。

## 一句话结论

比较靠谱的方向，不是继续维护一张越来越长的“字体名特判表”，而是：

- 用字体真实度量做主特征；
- 先做 shaping，再做测量；
- 按布局形态解约束，而不是按字体名拍 fallback scale；
- 把字体名表降级成最后兜底，而不是主路径。

换句话说：
**不要试图认识成千上万种字体；要让成千上万种字体先落到少数几类“可解释的度量行为”上。**

## 外部调研结果

### 1. OpenType 标准本身就支持“按度量校准”，不鼓励靠字体名猜

OpenType `OS/2` 表里已经给了很多你真正该关心的字段：

- `sTypoAscender`
- `sTypoDescender`
- `sTypoLineGap`
- `sxHeight`
- `sCapHeight`
- `xAvgCharWidth`

变量字体还可以通过 `MVAR` 让这些度量随轴变化。

这说明一个很重要的事实：
**字体“看起来差很多”这件事，本来就应该由字体度量解决，而不是靠调用方写一堆名字特判。**

但这只是外部标准给出的方向，不是 Phase 32 的可执行接口清单。当前插件不能改引擎源码，也不能直接依赖 DirectWrite / FreeType 的平台私有路径；第一版只能用 UE 公开 Slate 接口能稳定给出的布局观测值。把 `unitsPerEm / sTypoAscender / xHeight / capHeight` 这类字段写成本期已可取得，就是文档在画饼。

### 2. 真实文本尺寸应该建立在 shaping 之后

HarfBuzz 的定位很明确：先把 Unicode 文本变成 glyph 序列，再去谈排版和测量。
FreeType 的文档也反复强调，字符串尺寸不是简单的“字形宽度相加”，还会受 kerning、hinting、脚本 shaping 等因素影响。

这对 Design2UMG 的启发非常直接：

- 不能把文本测量问题简化成“字符数 * 字号 * 系数”；
- 不能把字体问题简化成“某个 family 固定缩到 0.71 / 0.73 / 0.75”；
- 真正可靠的路径，应该尽量依赖运行时实际 shaping / 测量结果。

### 3. Unicode 标准把“可断行位置”和“最终断哪”分成两层

UAX #14 给的是 line breaking algorithm，本质上先回答：
“哪些地方允许断行。”

它并不直接替你决定：
“给定这个宽度，最后应该断在哪一处，字号该是多少。”

这件事仍要交给布局系统自己求解。

所以更稳定的做法是分两步：

1. 先得到 break opportunities；
2. 再结合 box 约束和真实测量结果去选最终 layout。

### 4. Web 平台已经把“字体替换校准”产品化了

CSS Fonts Level 5 提供了一套非常有参考价值的能力：

- `font-size-adjust`
- `size-adjust`
- `ascent-override`
- `descent-override`
- `line-gap-override`

这套思路不是“给某个字体写 if 分支”，而是：
**当 fallback font 和目标 font 不一样时，用度量校准把视觉大小和行盒尽量拉回同一语义。**

这只能作为问题意识参考，不能当成 Slate 的实现模板。浏览器是在同一个排版引擎里做 fallback font 校准，Design2UMG 是在设计工具排版结果和 Slate/UMG 之间做跨引擎复现；Slate 侧也没有 per-text-block 的 `ascent-override / descent-override / size-adjust` 属性。Phase 32 能做的是用 Slate 观测值缩小求解空间，不是把 CSS descriptor 包一层搬进 UMG。

### 5. Android / Flutter 的工程实践更像“约束求解”，不是“经验常量拍脑袋”

Android `TextView` 的 autosize，是在给定边界内自动调字号。
Flutter `TextPainter` 暴露的是 `layout()`、`minIntrinsicWidth`、`maxIntrinsicWidth`、`preferredLineHeight`、`didExceedMaxLines` 这类接口。

这些成熟 UI 系统的共同点是：

- 先把文本放进一个真实 layout 系统里；
- 再根据宽高约束、换行、行数限制去解；
- 而不是先写死一张“字体 -> 缩放比例”表。

## 对当前 Design2UMG 的判断

当前仓库的文本路径已经有几个正确方向：

- 已经有 runtime probe；
- 已经开始强调 generic guard，而不是样例特判；
- 已经把一部分 cache reset 边界显式化；
- 已经在 formal gate 下长期验证文本回退风险。

按当前本地代码核对，几个事实不能写歪：

- `TextMeasurementRuntimeProbe.cpp/.h` 现在只有 `TryMeasureText()`、`TryMeasureTextBlockDesiredSize()` 以及上层 Measure/DesiredSize 包装，没有 fingerprint 接口。
- `TextMeasurementCache.cpp` 里相关缓存是 `ResolvedTextScaleByCalibrationKey`、`SharedStyleMinScaleByStyleKey`、`BaselineFallbackScaleByProbeKey`，不是一个可以随便叫的泛泛 “Bucket Cache”。
- `TextMeasurementCalibration.cpp` 里的 `FamilyFallbackScales` 当前只含 `geist=0.71` 和 `robotomono=0.73`。
- `ResolveTextFontSizeFallback()` 当前先取 family-specific scale，再在 measured scale 可用时用 `FMath::Min()` 收紧。

UE5.7 引擎源码也把 baseline 语义钉死了：

- `FSlateFontMeasure::GetBaseline()` 注释说它返回 bottom of max character height 到 baseline 的 offset。
- `FCharacterList::GetBaseline()` 注释说明这个 value will be negative。
- `SlateFontRenderer.cpp` 实现调用 FreeType descender。

所以当前还存在的明显风险是：

- `FamilyFallbackScales` 这类 family-based fallback 虽然只剩两个名字，但仍然在主路径前面；
- 一部分 heuristic 仍然更像“经验组合链”而不是“可解释的求解链”。

这条路短期能救火，长期会越来越难维护。

## 一条最值得参考的成功案例

我这轮额外调研后，最像我们问题、也最值得学的，不是某个“字体渲染论文 demo”，而是：

- **Web 平台的字体 metrics override 能力**
- **Next.js 把这套能力真正产品化后的落地方式**

原因很简单：

- 它们面对的也是“设计目标字体”和“运行时实际字体”不完全一致；
- 也必须处理大量未知字体，而不是几个固定字体；
- 它们最后跑出来的方案，依然是**度量校准 + 约束布局**，不是维护一张字体名黑魔法表。

### Web 平台是怎么做的

CSS Fonts Level 5 明确提供了几类能力：

- `font-size-adjust`
- `size-adjust`
- `ascent-override`
- `descent-override`
- `line-gap-override`

这套能力的核心思想非常值得注意：

1. **保留作者声明的语义字号**
2. **允许对实际使用字体的度量做校准**
3. **目标是让 fallback font 的视觉尺寸、行盒、布局行为更接近目标字体**

它不是在说：

- “碰到 Arial 就缩 0.73”
- “碰到某个 mono 字体就写特判”

它是在说：
**字体换了没关系，但度量要尽量对齐。**

### Next.js 是怎么把它产品化的

Next.js 在 `next/font` 里，已经把这套思路直接做成面向开发者的能力了：

- `adjustFontFallback`
- `declarations`

它的做法不是让用户自己维护一张超长字体特判表，而是：

- 自动启用 fallback 调整来减少 layout shift；
- 允许把 `ascent-override` 这种 metrics descriptor 写进生成的 `@font-face`。

这说明一个非常现实的工程结论：
**世界领先、用户量巨大的项目，最后也不是靠认字体名解决文本漂移，而是把“字体度量校准”做成主路径能力。**

## 这条成功案例对 Design2UMG 的直接启发

我们的问题和 Web/Next.js 很像，但不完全一样。

相同点：

- 设计稿中的名义字体和 UMG/Slate 最终真实使用到的字体度量不一定一致；
- 运行时还会受到字重、letter spacing、line height、wrap、ellipsis、fallback font 的影响；
- 一旦想靠“字体名 -> 固定比例”收口，就会越堆越多。

不同点：

- Web 平台可以在 `@font-face` 层直接 override metrics；
- Design2UMG 现在更多是在**解析到项目字体资产后的字号求解阶段**补偿；
- 我们现成的强项是已经有 `RuntimeProbe` 和一套求解器，不需要推翻重来。

所以最适合 Design2UMG 学的，不是“把 Web API 生搬硬套过来”，而是学它背后的方法论：

1. 不再把字体 family 当主键
2. 把字体度量当主特征
3. 把布局结果当最终判据
4. 把字体名表降到最后兜底

## 一个很值得优先做的产品策略

除了前面这套通用方案，我认为还有一条非常值得优先做，而且能立刻降低难度的路线：

- **优先让用户在真实 UE 项目里提供可加载字体资产**
- **只有用户没提供显式资产，或者资产实例不完整时，才走通用 fallback 链**

这条思路的核心不是“替代通用文本方案”，而是：
**先尽量把“未知字体问题”变成“已知字体实例问题”。**

### 这条路为什么值

很多文本漂移，根本原因不是求解器太弱，而是：

- 设计稿用的是 A 字体；
- 运行时实际命中的是 B 字体；
- 这两个字体的 ascent、descent、x-height、平均 advance，本身就不一样。

这种情况下，后面再怎么调 fallback scale，也很容易变成补丁大战。

所以产品层面最直接的降难方式就是：

1. 用户先把字体作为 `UFont` 资产放进真实 UE 项目；
2. Design2UMG 通过显式映射优先使用这些项目字体资产；
3. 只有缺字重、缺样式、缺 glyph 或根本没给显式资产时，才进入通用 fallback。

### 它能解决什么

它最擅长解决的是下面这类大误差：

- family 名字对上了，但实际加载到的是别的替代字体；
- 设计端使用了仓库本机字体，运行时环境没有；
- 字重、style、typeface 映射到了错误实例；
- 某些字体看起来“总是缩一圈”，本质上其实是压根不是同一个字体。

换句话说：
**它首先解决的是“字体身份不对”问题。**

### 它不能替代什么

即便用户提供了项目字体资产，下面这些问题仍然还在：

- 设计工具和 Slate/UMG 的 shaping、hinting、line box 计算不完全一样；
- 同一字体不同实例的 weight / width / optical size 仍可能有差异；
- letter spacing、line height、wrap、truncate 仍会影响结果；
- 字体资产不一定覆盖所有 glyph，CJK、emoji、特殊符号仍可能继续 fallback。

所以这条路最多只能把问题变成：

- 从“完全未知字体”
- 降成“已知字体实例 + 剩余布局差异”

它很好，但它不是终点。

## 对 Design2UMG 更合适的组合策略

我建议后面把文本路线明确成两层：

### 第一层：优先用户字体资产

目标很简单：

- 用户提供项目字体资产时，优先吃它；
- 不只认 family，还要尽量认准 typeface / weight / style / variable 实例；
- 缺资源时不要装作没事，要明确知道自己已经降级了。

这层优先解决的是：

- “是不是同一套字体资源”

### 第二层：通用度量校准与求解

即便命中了用户字体，最终字号和布局仍应由：

- metric fingerprint
- runtime probe
- wrap / truncate / line-height 约束
- autosize solver

共同决定。

这层解决的是：

- “即便字体对了，最终布局为什么还是会偏”

## 这件事应该怎么写进后续方案

如果后面单独开文本专项，我认为目标里应该明确加入一条：

- **字体资源优先级提升：用户显式提供的项目字体资产高于 family-name 推断和环境 fallback**

实现上更适合分成四步：

1. **字体资源解析**
   尽量把用户在真实 UE 项目中指定的 `UFont` 资产稳定解析成可命中的运行时字体资产。若未来要支持物理字体文件输入，必须另开 phase 处理导入和资产生命周期，不能把自动导入偷偷写回当前主路径。
2. **字体实例匹配**
   不只按 family 命中，还要看 typeface、weight、style、变量轴实例。
3. **实测求解**
   即便命中真实字体，也还是由 runtime probe 和 solver 定最终字号。
4. **通用 fallback**
   只有在字体缺失、实例缺失、glyph 缺失时，才走通用 fallback 链。

## 从项目收益看，优先级很高

这条线很值得优先的原因很现实：

- 它能先砍掉一大类最粗的误差源；
- 它不会和后面的 metric-driven 路线冲突；
- 它反而会让后面的通用方案更容易收敛，因为输入字体更真实了；
- 它也更符合用户直觉，用户在项目里指定了字体资产，你就应该优先按那套字体来。

所以我对这件事的判断是：

- **值得优先做**
- **但必须作为“前置降难层”，不是作为“文本问题已经解决”的替代说法**

## 对照当前代码，最适合怎么落

结合当前仓库现状，我认为这几件事最关键：

- `Source/Design2UMGCore/Private/TextMeasurementRuntimeProbe.cpp`
  已经能真实测 `MeasureTextSize()`、`MeasureUMGTextBlockDesiredSize()`、`MeasureSingleLineTextBlockHeight()`。
- `Source/Design2UMGCore/Private/TextMeasurementRuntimeProbeSizing.cpp`
  已经不是纯常量表逻辑了，而是有明确的宽度/高度 probe 和搜索求解。
- `Source/Design2UMGCore/Private/TextMeasurementCalibration.cpp`
  还保留着 `FamilyFallbackScales` 这种 family-based 兜底。
- `Source/Design2UMGCore/Private/TextMeasurementCache.cpp`
  当前 cache key 仍然强依赖 `NormalizedFamily` 和 `TypefaceName`。

这意味着：
**我们其实已经有一半走在正确路上了，真正还没转过来的，是“校准输入”和“缓存主键”的设计”。**

### 第一优先级：补一个字体度量指纹层

建议新增一个很薄的 `FontMetricFingerprint` 概念，不直接改变现有 solver，只改变它吃进去的“字体画像”。

这里必须分清两类东西，混在一起就是误导：

- OpenType 真度量：`unitsPerEm`、`sTypoAscender`、`sTypoDescender`、`sTypoLineGap`、`sxHeight`、`sCapHeight`、`xAvgCharWidth` 这类字段。
- Slate layout observation：公开 `FSlateFontMeasure` 能稳定拿到的是 Slate 排版系统观测后的指标，例如 `GetMaxCharacterHeight()`、`GetBaseline()` 和样本文本测量结果。

本期能靠公开跨平台接口落地的是 **Slate layout observation**，不是完整 OpenType 真度量。不要把 `FSlateFontMeasure` 写成能直接拿 `unitsPerEm / xHeight / capHeight` 的接口；它拿不到这些完整物理度量。完整 OpenType 度量只能作为长期方向，或者等后续另开更底层字体接口议题。

第一版指纹就够用，不需要做大而全：

- `GetMaxCharacterHeight()` 归一化结果
- `GetBaseline()` 归一化结果，但要保留符号，建议命名为 `SignedDescenderRatio`
- 分脚本样本文本的 advance ratio。拉丁/数字/标点样本和 fallback 脚本样本必须分开记录，不能混成一个平均值
- 度量是否可用、是否稳定

这些值**不能**直接调用 Windows 底层的 DirectWrite（会破坏插件的 Mac/Linux 跨平台编译）。现阶段应通过 Unreal Engine 原生且公开的跨平台接口 `FSlateFontMeasure` 获取 Slate 观测指标。更底层的 OpenType 字段不要在这个阶段假装已经可用。

更重要的是，这一层能天然覆盖：

- 不同字体名但度量很接近的字体
- 同一家族不同 weight / width / optical size 实例
- 变量字体不同实例

### 第二优先级：把 cache key 从“字体名字”升级成“布局行为”

当前 key 里保留 family/typeface 没问题，但不应该再让它们当绝对主角。

更合适的 key 结构应该逐步变成：

- metric fingerprint hash
- text shape category
- width / height constraints
- line-height mode
- letter-spacing mode
- wrap / truncate mode

这样缓存记住的是“这种度量行为下的求解结果”，而不是“这个名字以前大概缩过多少”。

这里还有一个不能装看不见的坑：进入 fallback scale cache key 的宽高约束、字号、letter spacing、line-height 等浮点输入，必须先做稳定归一化或有限精度量化，不能把布局过程里的原始浮点噪声直接拼进 key。`100.1` 和 `100.2` 这种微小扰动不该把缓存命中率砸穿；但真正会改变换行、截断或测量结果的约束差异仍要隔离。精确 `FVector2D` / actual measurement cache 是另一类缓存，不能拿这条规则去污染。

### 第三优先级：把 `FamilyFallbackScales` 改成最后兜底，不再先走它

更稳妥的迁移方式是：

1. 先让 fallback 优先参考 metric fingerprint + runtime probe；
2. 先让 runtime probe 按现有 line-box / width 约束求出 measured scale；probe 或度量拿不到时先使用通用 `DefaultFontFallbackScale`，只有无法取得 metric/probe 证据时，才允许最后回到 `FamilyFallbackScales`；formal/focused 回退只能证明实现还没做好，不能拿来给旧表开后门；
3. 每轮 formal gate 后，统计 family table 还有没有真实命中价值；
4. 命中越来越少时，再考虑彻底删除。

这样做的好处是：

- 不会一刀切把历史成绩打坏；
- 但主路径会逐步从“名字驱动”变成“度量驱动”。

### 第四优先级：把布局形态分类再往前提一层

当前仓库里已经有不少单行文本 heuristic，这个方向本身没错，但还可以再收口得更清楚：

- 数值/状态 token
- 单行标签
- 多行正文
- 垂直对齐且固定高的 wrapping 文本
- truncate / ellipsis 文本
- icon-font text

重点不是再加更多 if，而是让每一类只关心自己的目标函数。

例如：

- token 类优先保证单行和视觉密度；
- 单行标签优先宽度不溢出；
- 多行正文优先行数和 line box 稳定；
- ellipsis 文本优先截断行为稳定。

### 第五优先级：变量字体单独纳入度量体系，而不是当普通 family

这件事后面很容易变成新坑，所以最好一开始就在设计上留出来。

OpenType `MVAR` 已经明确说明，变量字体的 `sCapHeight`、`sTypoAscender`、`sTypoDescender`、`sTypoLineGap`、`sxHeight` 这些值都可能随实例变化。

所以后面如果只按：

- `FontFamily=Inter`

这种级别缓存或校准，会天然不够。

更合理的是把：

- weight
- width
- optical size
- 以及最终实例对应的 metrics

一起纳入 fingerprint。

## 我不建议怎么学 Web/Next.js

虽然这条案例很值得学，但我不建议直接走这几条：

### 1. 不建议照搬一层新的“大 override 表”

Web 有 `ascent-override`，不代表我们就该新造一张：

- `Family -> ascentScale`
- `Family -> capHeightScale`

的大表。

那只是把旧问题换个壳继续保留。

### 2. 不建议为了文本去重造一套独立 shaping 引擎

HarfBuzz / FreeType / DirectWrite / Slate 这条链很复杂。

对 Design2UMG 来说，更现实的做法不是自己重做 shaping，而是：

- 继续把 **Slate/UMG 真实测量** 当主事实来源；
- 再在它前后补“度量指纹”和“更稳的求解/缓存策略”。

### 3. 不建议先做跨平台大一统重构

从当前项目约束看，更实际的第一步是：

- 先用 Unreal 公开 Slate 接口在当前正式验证环境证明一套稳定、可解释、零过拟合的 metric-driven 路径；
- 等 formal gate 证明它确实比 family table 更稳，再考虑是否抽象到更通用层。

## 我认为最值得开的文本专项路线

如果后面单独做文本专项，我建议目标不要写成：

- “再优化几种字体”
- “把某几个 case 拉回来”

而是写成：

### 目标 1

建立 **font metric fingerprint + runtime probe** 的双证据主路径。

### 目标 2

让字号求解优先依赖：

- 度量
- 约束
- 实测布局

而不是依赖 family name。

### 目标 3

把 `FamilyFallbackScales` 压缩成历史兜底，并给出最终删除条件。

### 目标 4

把 cache key 从“字体名字缓存”改成“布局行为缓存”。

## 我建议的方案

### 方案核心

把文本主路径改成：

1. **布局形态分类**
2. **字体度量指纹**
3. **真实 shaping / 测量**
4. **约束求解**
5. **最后才兜底 fallback**

而不是：

1. 看字体名
2. 走一组 family-specific scale
3. 再叠别的 heuristic

### 1. 保留 runtime probe，但把它提升成主证据

当前 runtime probe 是很有价值的，因为它更接近最终 UMG/Slate 行为。

建议：

- 把 runtime probe 继续作为主测量路径；
- 无法 probe 时，才回退到保守 heuristic；
- 任何新的 fallback 规则，都应优先用 runtime probe 结果验证，而不是凭样例观感定数值。

### 2. 用“字体度量指纹”替代“字体名主键”

长期看，真正的 OpenType 度量当然有价值，但它不是当前公开 Slate 接口能直接给出的东西。本期不要把长期目标写成已可执行事实。当前建议先抽出下面这些 Slate 观测指标，构成一个 layout metric fingerprint：

- `GetMaxCharacterHeight() / ProbeFontSize`
- `GetBaseline() / ProbeFontSize`，保留负号，字段名用 `SignedDescenderRatio` 这一类清楚名字
- 固定样本文本宽度 / `(ProbeFontSize * 样本长度)`
- 度量可用性和稳定性标记

`sxHeight / unitsPerEm`、`sCapHeight / unitsPerEm`、`sTypoAscender / unitsPerEm`、`sTypoDescender / unitsPerEm`、`sTypoLineGap / unitsPerEm` 这类 OpenType 真度量只能放在长期方向里，不能写成本期 `FSlateFontMeasure` 主路径已经能拿。

目的不是做学术分类，而是把字体放进可分析的行为分布，例如：

- tall-ascender sans
- compact-line-box sans
- mono narrow
- mono wide
- display heavy

这样不同名字但度量接近的字体，可以复用同一套校准规则。这里说的是后续可分析的行为分布，不是 Phase 32 立刻要做粗聚类或 “bucket dispatch”。

### 3. 按“布局形态”而不是“字体 family”写规则

真正影响策略的，常常不是字体名，而是布局形态。

这里必须把边界说死：

- 允许使用源数据已经给出的布局/语义信号，例如宽高约束、wrap、truncate、最大行数、text growth、icon-font 语义。
- 禁止为了修分去解析文本内容，例如数空格、数大小写、数数字、按字符串内容判断 token/prose。
- 现有代码里若还有这类内容扫描，只能作为待收缩债务，不能成为 Phase 32 新增规则的依据。

建议至少分开：

- 固定宽单行标签
- 固定宽多行正文
- intrinsic 单行 UI label
- truncate / ellipsis 文本
- icon-font text

这样每一类都可以有自己的目标函数。

例如：

- 固定宽单行：优先不超宽、不超高、尽量接近声明字号
- 多行正文：优先保持可读 line box 和稳定换行
- intrinsic 单行 UI label：优先保持单行和视觉均衡，而不是盲目追求声明字号

### 4. 引入“小样本文本测量”，降低未知字体的不确定性

第一次遇到一个字体实例时，可以测一小组固定样本，快速建立该字体的经验画像。

建议样本：

- ASCII / 拉丁 / 数字 / 标点：`Agjy Hqx WWW iii 0123456789 .,;:!?-_/()[]{}`
- fallback 脚本观测：`中日かなカナ한`

可落成两个固定样本常量：

```cpp
TEXT("Agjy Hqx WWW iii 0123456789 .,;:!?-_/()[]{}")
TEXT("中日かなカナ한")
```

这不是为了做字体识别，而是为了知道：

- 这个字体的 line box 偏高还是偏紧；
- 宽字符和窄字符差异有多大；
- 数字、符号、字母混排会不会特别偏。
- fallback 脚本在当前 Slate composite font stack 下会走出什么观测结果。

严禁把西文字体本身不含的 CJK 字形混进同一个平均宽度里。那样测出来的是“目标字体 + 系统 fallback 字体”的混合物，Windows、macOS、Linux 或不同本地化环境下都可能漂，最后得到的不是指纹，是污染样本。

### 5. 用搜索/求解替代固定 fallback scale

对固定 box 的文本，更推荐的路径是：

1. 先确定 break opportunities；
2. 设定目标约束：
   - 不超宽
   - 不超高
   - 不超过 max lines
   - 尽量接近声明字号
3. 对字号做二分或步进搜索；
4. 选满足约束的最优解。

这样做的好处是：

- 行为更通用；
- 规则更容易解释；
- 比一组固定比例更不容易伤别的字体。

### 6. 把 `FamilyFallbackScales` 降级成最后兜底

我不建议粗暴一次性删掉它，因为 formal gate 里可能还依赖它的历史成绩。

更稳妥的迁移方式是：

1. 先新增 metric-driven 路径；
2. 让 family table 只在 metric path 无法得到稳定结果时再兜底；
3. 跑 focused + formal gate，看 family table 是否还能进一步收缩；
4. 最终再决定能不能完全移除。

### 7. 重做 cache key

当前缓存如果主要依赖字体名，会天然更容易过拟合。

建议后续改成更接近下面这种结构：

- metric fingerprint
- text shape category
- wrap mode
- width constraint
- max lines
- line-height mode
- truncate mode

这样可以让缓存更接近“布局行为”，而不是“字符串名字”。

进入 key 的约束值必须先归一化：有限固定小数、整数像素、0.5px 步长或其它经 focused test 证明不会制造误复用的格式都可以评估；原始 float 直接进 key 不行。否则所谓“布局行为缓存”会退化成“一点点浮点噪声一个新 key”，缓存命中率会被自己写死。

## 不建议怎么做

下面这些路，我不建议继续走：

### 1. 不建议扩 family 名字表

也就是不建议继续往 `geist`、`robotomono` 这种表里加更多字体名。

原因很简单：

- 用户字体可能成千上万；
- 这条路天然没有终点；
- 越往后越像补丁堆。

### 2. 不建议按字体名写专门逻辑

比如：

- `if (FontFamily == ...)`
- `Contains("mono")`
- `Contains("code")`

这类规则极容易过拟合，也和当前仓库明确反对的方向冲突。

### 3. 不建议把“声明字号”当运行时真值

source `fontSize` 和 UMG runtime font size 并不是天然一一对应。

继续围绕“必须尽量保留声明字号”去叠规则，很容易把别的 case 拉坏。

## 更适合 Design2UMG 的落地顺序

如果后面单独开文本专项，我建议按这个顺序推进：

1. 先落实用户字体资产优先，并记录实际命中的字体资产、实例和 fallback 状态。
2. 再把文本路径按“布局形态”切开，别再共用一大坨 heuristic。
3. 引入 metric fingerprint 和行为分布记录。
4. 把 runtime probe + autosize solver 组合成主路径。
5. 最后才逐步压缩 `FamilyFallbackScales` 这类历史兜底。

## 参考资料

- OpenType `OS/2` metrics table  
  https://learn.microsoft.com/en-us/typography/opentype/otspec184/os2
- OpenType `MVAR` metrics variations  
  https://learn.microsoft.com/en-us/typography/opentype/spec/mvar
- HarfBuzz shaping concepts  
  https://harfbuzz.github.io/shaping-concepts.html
- FreeType glyph metrics  
  https://freetype.org/freetype2/docs/glyphs/glyphs-3.html
- Unicode line breaking algorithm (UAX #14)  
  https://www.unicode.org/reports/tr14/
- CSS Fonts Level 5  
  https://www.w3.org/TR/css-fonts-5/
- Next.js `next/font`  
  https://nextjs.org/docs/pages/api-reference/components/font
- Android autosize text  
  https://developer.android.com/develop/ui/views/text-and-emoji/autosizing-textview
- Flutter `TextPainter`  
  https://api.flutter.dev/flutter/painting/TextPainter-class.html
- DirectWrite `IDWriteFontFace::GetMetrics`  
  https://learn.microsoft.com/en-us/windows/win32/api/dwrite/nf-dwrite-idwritefontface-getmetrics
- DirectWrite `DWRITE_FONT_METRICS1`  
  https://learn.microsoft.com/en-us/windows/win32/api/dwrite_1/ns-dwrite_1-dwrite_font_metrics1
