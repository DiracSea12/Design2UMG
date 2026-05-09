# Review 剩余项收口文档

本文件只做一件事：把 review 报告、路线图和 Phase 13 之后仍然需要判断的项说清楚。它不是用来删功能的，也不能把“写进文档”冒充成“代码已经实现”。

判断规则：

- 如果已经是当前代码契约，就写清楚为什么保留，并写清楚什么情况下才需要再动。
- 如果仍然要做，就写成可开发、可验收的工作项。
- 如果是产品路线候选，就保留为路线候选，不从路线图里抹掉。
- 是否进入实现，不再用“属于哪一期”当理由；只看产品价值、风险、验证成本和是否能不回退。

## 当前已经收口的事实

这些项不是“砍掉需求”，而是把 review 里不准确或过期的说法改成当前事实。

| 项 | 当前事实 | 证据 |
|----|----------|------|
| 属性类型化主路径 | `FUMGWidgetNode` 已经以 typed payload 为主，`FUMGPropertyBag` 是最终 UMG 消费桥，不是节点上的旧通用属性表 | `Docs/PropertyTyping.md` |
| `MaterialParams` / 渐变 | 渐变已经有 `FUMGVisualBrushData::FGradientData`、material ref 派生、material params 序列化和程序纹理 focused tests；这里不是“待拆分 MaterialParams”的开放问题 | `Docs/PropertyTyping.md`、`UMGWidgetNodeAccess`、`GradientProceduralTextureTests.cpp` |
| `UMGPropertyApplierInternal` 所在模块 | 当前产品路径是 editor-side 导入和 Widget Blueprint 生成，最终 `UWidget` mutation 留在 `Design2UMGEditor` 是当前契约 | `Docs/Architecture.md` |
| `DesignImportPipeline` 注释 | 已从笼统的“later tasks”改成当前 import-core seam / payload contract 描述 | `Source/Design2UMGEditor/Private/DesignImportPipeline.h` |
| Converter 内部注释 | 已给 converter 的非显而易见聚合/视觉分支补过定向注释；这不是全文件注释覆盖率承诺 | `69aa43f docs: clarify converter internals` |
| public boundary 扫描 | 已有公开面检查，负责扫描公开文档、脚本、Source、Config、`.github` 等位置的维护者绝对路径、HostProject 字符串和本地工作区泄漏 | `Tools/Release/Assert-PublicRepoBoundary.ps1` |
| runtime 验证入口 | 自动化入口已经要求显式 `--project` / `--editor` 或对应环境变量，并拒绝 HostProject fallback；maintainer formal gate 与 public smoke 也已经在验证文档中区分 | `Tools/design2umg_auto_verify.py`、`Tools/design2umg_auto_verify_batch.py`、`Tools/Run-*.ps1`、`Docs/ValidationProtocol.md` |

## 仍然要按真实开发处理的项

下面这些不能只靠文档完成。要做就按验收口径做；不做也要保留为明确的路线候选或当前非目标，而不是靠删文字来消失。

### 1. 公开 CI / Release / Coverage 可见度

当前状态：

- `.github/workflows/build.yml`、PR 模板、Issue 模板、CODEOWNERS、Dependabot 已存在。
- `.github/release.yml` 已提供 GitHub generated release notes 分类；它只负责发布说明分类，不代表公开托管了完整 UE formal gate。
- `Tools/Release/ReleaseCandidateChecklist.md` 和 `Tools/Release/PublicRepoExportWorkflow.md` 已补充候选 commit、BuildPlugin 包、边界扫描、release notes、artifact 上传和 gate 证据分层。
- README 已明确：当前没有公开 hosted CI matrix、coverage badge、release asset pipeline。
- UE5 完整 formal 47-case gate 依赖维护者机器和真实项目，不能伪装成公开 hosted gate。

要做的真实工作：

- 把 public smoke 能公开跑的部分放到 GitHub Actions：文档/边界检查、脚本语法检查、包装插件的 self-hosted 或手动工作流。
- 已有 `Tools/Release/ReleaseCandidateChecklist.md`，不要另造一套 release checklist；后续只需要按真实发布结果继续维护这份清单。
- 如果做 coverage，只能先定义可采集的范围；不要随口写 60% / 80% 这种没有工具链支撑的数字。

验收标准：

- README / CONTRIBUTING / `Docs/ValidationProtocol.md` 明确区分 public smoke、self-hosted BuildPlugin、maintainer formal admissible gate。
- GitHub Actions 或 release 文档能被贡献者读懂，失败时能告诉用户缺什么。
- 不生成、不依赖、不暗示任何仓库禁止的临时验证工程 fallback。
- 不把 public smoke 写成“完整 release gate 已公开托管”。

### 2. Prototype Flow 的 UEdGraph authoring 和 Tag 方案

当前状态：

- 当前主线已经有 typed navigation contract + Blueprint extension，支持多页面 `WidgetSwitcher`、点击导航、特殊动作、hover/press、部分过渡动画。
- 本轮已新增生成模式：`RuntimeExtension` 默认保持现有运行时扩展，`TagHelper` 生成 data-only metadata 并由 helper 绑定基础点击切页，`Disabled` 不生成导航绑定。
- `Docs/ROADMAP.md` 里保留的 `BlueprintGraphHelper` / UEdGraph authoring 不是被删除的功能，而是和 Tag helper 并列的产品路线候选；本轮选择先落地 Tag helper 路线。

要做的真实工作：

- 已选择 Tag 方案：生成稳定 metadata，并提供 `SetupNavigation(UWidgetSwitcher*, UUserWidget*)` 复用 helper。
- UEdGraph authoring 只有在产品明确选择图表节点路线时再做；它不是 Tag helper 路线的验收前置。

验收标准：

- 生成的 Widget Blueprint 能编译。
- 自动化覆盖默认 runtime extension、TagHelper 点击后 `WidgetSwitcher` 切页、Disabled 不绑定；已有 runtime extension 测试继续覆盖打开链接、返回、overlay、hover/press。
- 不按 case 名、节点 id、文件名、组件名写分支。
- 不破坏当前 typed runtime extension 的测试和 formal 47-case `no_regression`。

### 3. `SharedCoreSemanticsTests.cpp` 的位置和组织

当前状态：

- 文件已从 `Source/Design2UMGCore/Private/SharedCoreSemanticsTests.cpp` 移到 `Source/Design2UMGCore/Private/Tests/SharedCoreSemanticsTests.cpp`。
- 本轮只做测试组织迁移，没有为了路径好看扩大 public include 或模块依赖。

要做的真实工作：

- 已完成移动。后续如果测试发现规则有问题，修测试发现规则，不回退到 Private 根目录。

验收标准：

- Core 测试仍能被 Unreal automation 发现和运行。
- 不新增 public include 或模块依赖来迁就测试位置。
- 迁移前后相关测试通过；文档或测试命名能让贡献者一眼知道它是 Core semantics 测试。

### 4. `bHas*` presence bits 的清理

当前状态：

- `UMGSchemaTypedPayloads.h` 里仍然有大量 `bHas*`。
- 其中一部分已经有明确注释：parser、serializer、typed descriptors 依赖显式 set/unset 语义，例如空字符串和 unset 不是同一个意思。
- 本轮已把 `FUMGTextStyleData::LetterSpacing` 从 `float + bHasLetterSpacing` 收缩为 `TOptional<float>`，并覆盖显式 `0`、非 `0`、unset 的 round-trip / typed bag / applier 语义。
- 其它 `bHas*` 暂不批量替换，不是拖延，而是它们现在仍承担明确 typed contract presence 语义。

要做的真实工作：

- 只按字段迁移。每个字段先证明它不需要 UE 序列化、不需要 typed descriptor presence、不需要区分显式空值和未设置。
- 新增字段默认不要再写 `bHasX + X`，除非有明确 serializer / typed contract 原因。

验收标准：

- 每个迁移字段有 parser / serializer round-trip 或 typed property bag 测试。
- 空值、零值、false、unset 的语义不变。
- affected applier / binder 测试通过。
- formal 47-case gate 不出现 historical-best regression。

### 5. 实现文件注释和复杂逻辑说明

当前状态：

- review 对“复杂实现缺少注释”的方向是合理的。
- 已补的是 converter 中最容易误读的内部聚合/视觉分支注释，不是把所有实现文件都刷一遍注释。

要做的真实工作：

- 只给非显而易见的算法、边界判断、历史坑位和跨模块契约补注释。
- 不给直白代码补废话注释；不把注释当作替代重构的手段。
- 不在注释里把参考图、对比图、节点裁图写成主流程方案。

验收标准：

- 对 `Design2UMGConverter*`、`FigmaParserInternal*`、`Design2UMGPrototypeStepGraphAssembly.cpp`、`VisualMapper.cpp` 等热点文件，触碰复杂逻辑时必须补一句能解释“为什么这样做”的注释。
- 新注释和代码事实一致，不能出现“未来再做”式空话。
- `git diff --check` 通过，中文可读，文件保持 UTF-8 with BOM。

### 6. `UMGPropertyApplierInternal` 是否要搬模块

当前状态：

- 当前不搬。因为它直接应用到 `UWidget`，而产品主线是 editor-side Blueprint 生成，不是 runtime module。
- 这不是延期，也不是回避；这是当前 supported product scope 的边界。

要做的真实工作：

- 只有当项目真的要支持 runtime module，或者把 `UWidget` mutation 从 editor generation 中独立出来时，才启动模块迁移。
- 迁移前要先定义新的 `UWidget` mutation contract；不能只是把文件挪到 Core。

验收标准：

- Core 依赖不引入 `UnrealEd` / `UMGEditor`。
- Editor 仍能生成 Widget Blueprint。
- 公共 API 不因为搬文件扩大。
- formal 47-case gate 不回退。

### 7. 渐变 / 材质能力扩容

当前状态：

- 当前主路径不是 `Content/Materials` 材质资产路线，而是 `MaterialRef` 作为程序纹理 key，最终由 `ProceduralShapeGenerator` 生成 transient texture。
- Linear 已通过 `gradientlinear + Rotation + Stops` 支持多色 stop 和任意角度。
- Radial 已通过 `radialgradient + CenterX/CenterY + SizeWidth/SizeHeight + Rotation + Stops` 支持中心点、径向尺寸、旋转和多色 stop。
- Angular 也有独立 `angulargradient` 程序纹理分支。
- 本轮已补 focused tests，覆盖 Figma parser 几何、typed payload/material params、linear multi-stop 任意角度和 radial center/size/rotation 程序纹理入口。
- 仍保留的路线候选是更完整的材质资产/SDF 系统，但这不是当前已采纳 release 路线。

要做的真实工作：

- 当前收口已补 focused tests，证明 parser -> typed payload -> material params -> 程序纹理入口的链路。
- 如果未来再走材质资产路线，先解决 `CanContainContent`、材质资产依赖、卸载插件后的资产可用性和 UI material domain。
- 程序纹理路线要继续明确质量边界，不要把它包装成完整材质系统。

验收标准：

- 多色 stop、角度、中心点、径向尺寸都有 parser -> typed payload -> UMG 应用链路。
- 不通过样例定向阈值或 case 分支换分。
- formal 47-case gate 不回退；新增视觉能力要有 focused render 证据。

## 不该做的“收口”

- 不删除 `Docs/ROADMAP.md` 里的 Prototype Flow、BlueprintGraph、Tag 项来制造“已经统一”的假象。
- 不把 public smoke 写成完整公开 CI。
- 不把 `bHas*` 全量替换成 `TOptional`。
- 不为了测试文件路径好看扩大模块边界。
- 不为了注释数量刷无意义注释。
- 不用 Phase 编号解释为什么某个明确问题不处理。

## 本轮执行状态

本轮按风险从低到高已经执行：

1. 已补 GitHub Release notes template / 公开 artifact 发布流程，并更新现有 `Tools/Release/ReleaseCandidateChecklist.md`。
2. 已把 `SharedCoreSemanticsTests.cpp` 移到 Core private tests 目录。
3. 已完成 `LetterSpacing` 字段级 presence bit 收缩，配套 round-trip、typed bag 和 applier 测试。
4. 已实现 Prototype Flow 的 RuntimeExtension / TagHelper / Disabled 生成模式；TagHelper 走 data-only metadata + helper 绑定路线。
5. 已补渐变程序纹理 focused tests；材质资产/SDF 仍是单独产品路线，不算当前 release 路线的未完成项。

## 本轮验证结果

- 真实项目编译已通过：`FPSRoguelikeEditor Win64 Development -Project=<maintainer real project> -NoHotReloadFromIDE`。
- focused automation 已通过，覆盖 Prototype 三模式、TagHelper、LetterSpacing、typed payload、渐变程序纹理和 shared semantics；日志为 `Saved\Logs\Phase13Remaining-Focused-Rerun.log`。
- formal 47-case admissible gate 已通过，输出为 `Saved\Design2UMG\Automation\_formal_full_47_20260510_phase13_remaining_current`。
- formal 结果：发现 47 个 case，报告 47 个 case，0 blocked，`no_regression` 为 47/47，通过 historical-best 校验，未发现 regression。
