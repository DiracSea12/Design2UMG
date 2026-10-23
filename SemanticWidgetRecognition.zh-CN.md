# 控件语义识别：设计方案（草案）

状态：提案，未实现。本次只更新方案，不修改代码；自动识别的具体规则、默认应用条件和未定选择留待后续讨论。现有能力与新增目标分别说明，本文不是实现完成或验收通过的记录。

## 1. 问题

现有创建表已经包含多种交互控件，表外还可以动态查找控件类；但“类能创建”不等于“设计稿能转换成可用控件”。Figma 和 Pencil 的节点主要是 Frame、Rectangle、Text、Path、Ref 等视觉图元（见 `EDesignNodeType`）。设计稿里的“复选框”通常是方框、对勾和文字的组合，还需要确定类型、部件和状态。

当前属性应用表尚未接入 `CheckBox`、`Slider`、`ProgressBar`、`SpinBox` 等控件的专属样式和数值。Figma 解析链已保留实例组件属性与变体属性，可以复用；真正需要补齐的是从源数据、部件识别到样式、初始值、行为和重导入的完整链路，而不是只扩充控件注册表。

目前只有三条路能生成交互控件：

- 原型连线：触发节点外包一层 `Button`（`Design2UMGPrototypeNavigation`）。
- Widget Class 覆盖：按节点名替换类，但并不自动完成该类所需的部件、结构和专属样式适配。
- 组件映射：整棵子树替换成项目已有的 WBP。

结果是：用户拿到的“按钮”“滑块”“输入框”在视觉上正确，但在 UE 里不可交互，需要手工重做。这正是生成结果和“可直接用”之间最大的差距。

### 必须遵守的现有原则

`PenParserNodeExtensions.cpp` 里已经写明：**名称是标签，不是渲染指令；只有显式的语义字段才能替换作者画的内容。** 本方案保留单凭普通名称不改变生成结果的约束；显式标注是指令，普通名称只是识别证据之一，自动转换还需结合结构、状态或交互等证据。具体识别规则后续讨论，不把控件名是否出现作为唯一入口。

生成资产使用引擎原生 UMG，或用户明确选择的引擎 CommonUI 控件及生成的蓝图、样式资产；不引入 Design2UMG 自定义运行时控件依赖。修改范围限定在插件及自身导出链，不修改引擎源码，不使用参考图、对比图或节点裁图补全控件外观。

## 2. 目标与非目标

**目标**

- **48 种原生 UMG 控件全部接入的目标保留**：设计者做了标注，就按标注生成，不论哪一类；目标引擎缺少该类型时明确报告，不静默改成其他控件。
- **CommonUI 全量接入**：第 3.5 节列出的 32 个生成目标全部纳入范围，包括需要显式标注或项目配置的第三类；在用户选择 CommonUI 且项目具备对应能力时生成其控件、内容和样式，逐版本核对属性、接口与运行前提，不以部分类型代替全量接入。
- **没有标注时，前两类共 32 种尽量自动识别并转换的目标保留**，并保持默认状态的视觉一致和控件行为正确。自动识别也服务于 CommonUI 生成目标，不另做一套重复识别规则。
- 每一次识别都可解释（命中了哪条规则、依据哪些信号），可关闭，可逐节点否决。
- 与组件映射、Widget Class 覆盖、原型交互、安全重导入的现有语义不冲突。
- 支持范围按引擎版本、可用插件、控件及属性分别记录；分组实施只改变实现顺序，不缩减全部控件接入和尽量自动识别的目标。

**非目标**

- 不推断项目业务逻辑。控件自身的状态响应属于适配范围；页面业务、数据来源和输入动作等需要项目明确配置，缺少时如实报告。
- 不用 AI 或图像识别猜测控件，只使用确定性规则。
- 第三类 16 种不做自动推断，只按标注生成，因为设计数据里没有它们的信息。
- 本次不实现识别器、控件适配或测试工具，不运行设计稿回归，不将识别细节及默认档位写成已确认决定。

## 3. 识别哪些控件

### 3.1 普通 UMG：保留 48 种接入目标

下表沿用上一轮依据 UE 5.8.3 整理的 48 种目标清单，不含抽象基类和 Slot 类。这是既有需求范围，不代表当前 UE 5.7 或所有版本的完整库存已重新核实。开发前须按目标引擎核对可用性及属性差异，保留既有目标，不为维持三组各 16 种而硬套分类。CommonUI 另见第 3.5 节。

| 类别 | 数量 | 控件 |
| --- | --- | --- |
| 可直接映射的视觉控件（布局与外观） | 16 | `CanvasPanel`、`HorizontalBox`、`VerticalBox`、`WrapBox`、`GridPanel`、`UniformGridPanel`、`Overlay`、`SizeBox`、`ScaleBox`、`Spacer`、`Border`、`Image`、`TextBlock`、`RichTextBlock`、`BackgroundBlur`、`RetainerBox` |
| 本方案的识别对象（含已部分生成的） | 16 | `Button`、`CheckBox`、`ExpandableArea`、`WidgetSwitcher`、`ComboBoxString`、`NamedSlot`、`ScrollBox`、`ProgressBar`、`Slider`、`EditableTextBox`、`MultiLineEditableTextBox`、`SpinBox`、`Throbber`、`CircularThrobber`、`ListView`、`TileView` |
| 设计数据里没有对应信息 | 16 | `ComboBox (Object)`、`ComboBoxKey`、`DynamicEntryBox`、`EditableText`、`MultiLineEditableText`、`InputKeySelector`、`InvalidationBox`、`MenuAnchor`、`NativeWidgetHost`、`PostBufferUpdate`、`SafeZone`、`ScrollBar`、`StackBox`、`TreeView`、`Viewport`、`WindowTitleBarArea` |

处理方式：

| 类别 | 有标注 | 无标注 |
| --- | --- | --- |
| 第一类（可直接映射的视觉控件） | 按标注生成 | 普通 UMG 复用现有转换路径；CommonUI 需补对应适配，见第 3.5 节 |
| 第二类（交互与数据控件） | 按标注生成 | 尽量识别并转换；证据或运行前提缺失时报告候选及缺口（第 3.2 节） |
| 第三类（无设计信息） | 按标注生成（第 3.3 节） | 不推断 |

第三类要么是数据驱动（`DynamicEntryBox`、`TreeView`、`ComboBox (Object)`），要么是性能或平台层面的决定（`InvalidationBox`、`SafeZone`、`WindowTitleBarArea`），要么只是同类控件的无外框版本（`EditableText`）。设计稿里没有这些信息，自动推断只会制造误判，所以只接受标注。

### 3.2 识别清单

下表保留各类控件的信号调查和接入目标。置信度列是早期提案判断，不是实测准确率或默认放行结论；自动应用条件、规则与具体识别顺序后续讨论，并与第 9 节的逐组完整接入顺序区分。

| 阶段 | 控件 | Figma 信号 | Pencil 信号 | 置信度（Figma / Pencil） | 说明 |
| --- | --- | --- | --- | --- | --- |
| P1 | `NamedSlot` | `SLOT` 组件属性 | frame 的 `slot` 字段（解析器已读取） | 待核实 / 待核实 | 源侧插槽或推荐组件信息不直接等同于 UMG NamedSlot；须核对内容承接关系 |
| P1 | `Button` | 组件集 `VARIANT` 属性含 Default / Hover / Pressed / Disabled 等状态；`MOUSE_ENTER` / `ON_HOVER` / `ON_PRESS` 时 `CHANGE_TO` 兄弟变体；带 `ON_CLICK` 动作 | 状态族 `Button/<状态>`、`Icon Button/<尺寸>/<样式>` | 高 / 中高 | 已有的原型 Button 包装要合并到这条路径，避免两层按钮 |
| P1 | `CheckBox`（含 Switch、Radio 外观） | `VARIANT` 或 `BOOLEAN` 属性表示选中状态（Checked / On / Selected）；`ON_CLICK` 时 `CHANGE_TO` 兄弟变体，或 `SET_VARIABLE` 切换布尔变量 | 状态族 `Checkbox/Checked\|Unchecked`、`Switch/…`、`Radio/Selected\|Unselected`；`check` 图标 | 高 / 中高 | UMG 没有单选组。Radio 生成 `CheckBox`，并提示互斥逻辑需要项目实现 |
| P2 | `ExpandableArea` | 变体 Expanded / Collapsed（或 Open / Closed）+ `ON_CLICK` 时 `CHANGE_TO` | 状态族 `Accordion/Open\|Closed`；`chevron` 图标 | 高 / 中高 | |
| P2 | 页签（`WidgetSwitcher` + `Button` 组） | 一组 Selected / Unselected 变体 + `ON_CLICK` 时 `NAVIGATE` 或 `SWAP` 到内容页 | `slot` 推荐组件为 `Tab Item/Active\|Inactive` | 高 / 中高 | 复用已有的 `WidgetSwitcher` 生成 |
| P2 | `ProgressBar` | 没有语义字段；靠结构（轨道 + 部分宽度的填充），填充宽度绑定了 `FLOAT` 变量时更可靠 | `Progress` 组件（轨道 frame + 半宽 rectangle） | 中 / 中 | 从填充比例推出 `Percent` |
| P2 | `EditableTextBox` / `MultiLineEditableTextBox` | 没有语义字段；靠结构（带边框容器 + 单个弱化颜色的文本） | 状态族 `Input Group/…`、`Textarea Group/…`、`Input OTP …` | 中 / 中高 | 占位文本作为 `HintText`；多行由高度和换行决定 |
| P3 | `ComboBoxString` | Open / Closed 或 Default / Filled 变体 + `ON_CLICK` 时 `OVERLAY` 下拉列表 | 状态族 `Select/Default\|Filled` + `Dropdown`（`slot` 列表项）；`chevron-down` 图标 | 中高 / 中 | 选项取自下拉列表中的文本 |
| P3 | `Slider` | 靠结构（轨道 + 填充 + 滑块头）；`ON_DRAG` 可作辅助 | 靠结构 | 中 / 中 | 当前 halo 设计系统里没有滑块组件 |
| P3 | 悬停提示（`ToolTipWidget` 绑定） | `MOUSE_ENTER` / `ON_HOVER` 时 `OVERLAY` | `Tooltip` 组件 | 中 / 低 | 不是独立控件，而是给目标控件设置提示 |
| 已实现 | `ScrollBox` | `overflowDirection` | 无对应字段；靠结构（固定尺寸、`clip` 开启、内容超出） | 确定 / 中 | Figma 部分已在转换器中生成 |
| P3 | `SpinBox` | 结构：输入框 + 上下箭头或加减按钮 | 同左 | 中 / 中 | 数值取自输入框文本 |
| P3 | `Throbber` / `CircularThrobber` | `AFTER_TIMEOUT` 循环切换的变体 + 结构（同形状元素等距排列，或沿圆周排列） | 结构 + 名称 | 中 / 低 | 用设计中的单个元素作为 `PieceImage`，元素个数作为 `NumberOfPieces` |
| P3 | `ListView` / `TileView` | 同一组件的重复实例（纵向或换行排列）；`SLOT` 的 `preferredValues` | `slot` 推荐组件 + 重复引用（如 `Table Row`、`List Item`、`Pagination Item`） | 中 / 中 | 见下方说明 |

**列表类的转换方式**：保留 `ListView` / `TileView` 等的标注接入和自动识别目标，但区分识别出列表候选与真正应用转换。重复排列本身不确定数据来源；转换前需要明确条目类及数据接入方式。普通 `ListView` 条目还需要引擎要求的列表条目接口，不是只设置 `EntryWidgetClass` 就完成。显式标注时按类型生成并报告缺少的前提；未标注且数据前提缺失时保留原视觉内容、报告候选，不把“设计器有预览、运行时为空”算成成功转换。使用 `semantic_runtime_data_required` 写明需要项目接入的内容。

### 3.3 第三类：仅按标注生成

| 控件 | 标注后的生成方式 | 引擎版本 |
| --- | --- | --- |
| `EditableText` / `MultiLineEditableText` | 与第二类的文本框相同，但不带外框 | 5.0+ |
| `ComboBoxKey` | 外观同 `ComboBoxString`；选项键取自文本 | 5.0+ |
| `ComboBox (Object)` | 外观同 `ComboBoxString`；选项需要项目在运行时提供 | 5.0+，实验性 |
| `InputKeySelector` | 按钮外观；默认键取自文本 | 5.0+ |
| `StackBox` | 替代 `HorizontalBox` / `VerticalBox`，方向取自自动布局 | 5.0+ |
| `SafeZone` / `InvalidationBox` / `WindowTitleBarArea` | 作为包装层套在被标注的节点外面 | 5.0+ |
| `MenuAnchor` | 被标注节点作为锚点；原型中 `OVERLAY` 弹出的内容作为菜单 | 5.0+ |
| `DynamicEntryBox` / `TreeView` | 同列表类：条目类取自重复组件，运行时需要数据 | 5.0+ |
| `ScrollBar` | 轨道和滑块外观取自设计 | 5.0+，实验性 |
| `Viewport` / `NativeWidgetHost` | 生成占位，并保留设计中的尺寸 | 5.0+；`Viewport` 为实验性 |
| `PostBufferUpdate` | **仅用户显式标注时转换，始终不自动推断**；在目标引擎支持时生成于被标注的位置，不绘制内容 | 已核对 5.0.3–5.3.2 的对应版本源码无该类，5.4.4 等版本有，详见第 3.4 节 |

### 3.4 引擎版本支持

2026-10-17 按本机各版本的 `Engine/Build/Build.version` 和 `Engine/Source/Runtime/UMG/Public/Components/PostBufferUpdate.h` 核对：

| 已核对版本 | `PostBufferUpdate` |
| --- | --- |
| 5.0.3、5.1.1、5.2.1、5.3.2 | 源码中没有该头文件 |
| 5.4.4、5.5.4、5.6.1、5.7.1、5.8.2 | 源码中有该类 |

其中 5.7.1 是当前工作区对应引擎，其余来自本机 `EngineCompatibility` 中对应版本。上述证据是源码可用性检查，不是本次编译或运行验证；精确首次引入的小版本仍待确认。

- `PostBufferUpdate` 始终属于仅显式标注转换的第三类，是否存在都不参与自动推断。
- 普通 UMG 和 CommonUI 均建立分版本的类型、关键属性、接口及插件前提清单；有类不代表各版本全部行为相同。
- 编译时隔离目标版本缺少的类型及接口，低版本不直接引用缺少的头文件或符号。引擎版本适配不扩展为旧 JSON、旧配置或旧行为兼容链。
- 目标引擎缺少类型时输出 `semantic_unavailable_in_engine`，写明实际版本及已核实的支持范围。该标注未完成转换，禁止静默换类或计为成功；是否保留视觉内容作为未完成结果，须明确记录。
- 对引擎标为实验性的控件明确提示其状态；是否需要额外开关留待讨论，不以隐含默认禁用违背标注接入目标。

### 3.5 CommonUI：完整清单与三类划分

CommonUI 是引擎自带的可选运行时插件，也是普通 UMG 之外的生成目标，不单独构成第四类。其控件仍按“设计数据能确定什么”划分为三类。**分类与实现状态分开**：第一类表示可以直接映射，不表示当前插件已经完成 CommonUI 支持。以下是本项目的接入策略划分，不是 Epic 官方分类，具体识别规则仍待讨论。

#### 清单口径与版本证据

2026-10-17 对照本机 UE 5.7.1 的 `Engine/Plugins/Runtime/CommonUI/Source/CommonUI/Public` 及其子目录，按公开 UCLASS 声明和继承关系识别出 35 个 UWidget 派生类。以下三项不作为独立生成目标：

| 源码类型 | 不单列的原因 |
| --- | --- |
| `UCommonButtonInternalBase` | CommonUI 内部按钮实现，由其上层控件使用，不作为设计稿的独立目标 |
| `UDEPRECATED_UCommonVisibilityWidgetBase` | 已弃用类型，不新增对其生成逻辑的依赖 |
| `UCommonActivatableWidgetContainerBase` | 抽象容器基类，通过下表的 Stack / Queue 派生类型接入，不另算一种直接生成结果 |

因此本次完整生成目标为 **32 个：第一类 3 个、第二类 9 个、第三类 20 个**。其中包含需要生成派生蓝图的基类；样式类、Slot、输入数据、分组对象、接口及子系统另记为必要配套能力，不混算控件数量。

逐项对照本机 UE 5.0.3 的对应 CommonUI 公开类声明，这 32 个目标类型也均存在。这只证明两端版本存在对应类，不表示全部属性、接口和运行行为一致，也不是本次编译或运行验证。其余支持版本及字段差异继续按第 3.4 节核对。

#### 第一类：可直接映射的视觉控件（3 个）

用户选择 CommonUI 后，复用已知视觉语义，补齐内容、样式资产及属性适配，不额外猜测项目行为。

| 控件 | 转换依据与接入内容 |
| --- | --- |
| `CommonTextBlock` | 普通文字、字体和文字样式资产 |
| `CommonRichTextBlock` | 富文本内容、样式及必要装饰信息 |
| `CommonBorder` | 边框、背景、内容容器及样式资产 |

#### 第二类：尽量识别结构与关系后转换（9 个）

这一类保留尽量自动识别的目标。识别出语义后，仍须满足具体控件的部件、数据和生成前提，不把“识别出来”与“已经适配完成”混为一谈。

| 控件 | 需要识别或补齐的信息 |
| --- | --- |
| `CommonButtonBase` | 按钮内容、外观状态和交互；生成派生控件蓝图及样式资产 |
| `AnalogSlider` | 轨道、滑块、方向和初始数值；补齐对应控件行为 |
| `CommonHierarchicalScrollBox` | 滚动容器、布局方向与内容关系，并核对导航行为 |
| `CommonRotator` | 可切换的文字选项、当前选中项及切换关系；它不是旋转图片 |
| `CommonTabListWidgetBase` | 页签按钮、内容页对应关系；生成派生蓝图并承接注册和切换 |
| `CommonWidgetCarousel` | 轮播内容、页面关系及明确的切换行为 |
| `CommonWidgetCarouselNavBar` | 轮播导航项及其关联的 Carousel，不凭一排圆点直接确定 |
| `CommonListView` | 重复条目、条目类和数据来源；保留第 3.2 节的数据前提约束 |
| `CommonTileView` | 平铺条目、条目类和数据来源；保留第 3.2 节的数据前提约束 |

#### 第三类：需要显式标注或项目配置（20 个）

这些类型的额外语义通常不体现在普通静态外观中。明确的项目配置也算用户选择，例如统一指定页面基类，不要求对每个实例重复标注。支持范围仍包含全部类型，不因需要配置就删去接入目标；配置缺项则报告未完成内容。

| 控件 | 需要明确的信息 |
| --- | --- |
| `CommonUserWidget` | 选择该页面或组件基类，以及项目输入处理需求 |
| `CommonActivatableWidget` | 页面激活、停用、焦点和输入路由策略 |
| `CommonAnimatedSwitcher` | 选择该切换实现、过渡方式及子页面激活关系，不只凭多页面外观选择 |
| `CommonActivatableWidgetSwitcher` | 对激活页面的切换策略和生命周期要求 |
| `CommonVisibilitySwitcher` | 按可见性切换子项及激活可见内容的明确策略 |
| `CommonActivatableWidgetStack` | 页面入栈、出栈、返回和激活关系 |
| `CommonActivatableWidgetQueue` | 页面排队、推进和激活关系 |
| `CommonActionWidget` | 关联的输入动作及平台图标数据，不把静态手柄图标当成动作绑定 |
| `CommonBoundActionBar` | 从输入处理系统收集动作的规则、条目类及数据配置 |
| `CommonBoundActionButton` | 关联动作、必要绑定部件和派生蓝图 |
| `CommonCustomNavigation` | 项目自定义导航关系或事件处理 |
| `CommonHardwareVisibilityBorder` | 平台、输入设备或硬件条件下的可见性策略 |
| `CommonLazyImage` | 图片软引用及异步加载需求，不由普通图片外观推断 |
| `CommonLazyWidget` | 要异步加载的控件类、软引用及加载需求 |
| `CommonLoadGuard` | 加载状态、被保护内容及加载提示关系，不把加载图案直接当成资源保护策略 |
| `CommonDateTimeTextBlock` | 日期或时间数据、格式及更新需求 |
| `CommonNumericTextBlock` | 数值数据、格式和可能的插值行为，不只凭一段数字选择 |
| `CommonTreeView` | 层级数据、条目类和数据接口，沿用普通 TreeView 的显式接入范围 |
| `CommonVideoPlayer` | 视频源、播放和资源配置，不把图片占位当成视频语义 |
| `CommonVisualAttachment` | 附件不计入主体计算尺寸的布局语义，不只凭两个元素重叠选择 |

以上分类表达当前常规设计数据下的接入策略，不代替后续的识别规则讨论；新增明确源证据时再核对其实际含义和应用前提，不凭空补出项目行为。

#### 生成与验收边界

- 普通 UMG 保持默认生成目标；用户可明确选择 CommonUI，适用的识别结果走 CommonUI 适配路径。识别判断按钮、文字、页签等语义，不重复实现两套识别规则。
- 显式标注支持具体类型，例如 `[d2u:CommonButtonBase]`，优先于生成目标的默认选择。项目缺少 CommonUI 时明确报告所需插件和未完成项，不自动启用插件或静默改回普通 UMG。
- `CommonButtonBase`、`CommonTabListWidgetBase`、`CommonBoundActionButton` 需要派生蓝图；`CommonButtonStyle` 等样式基类也需要相应派生资产，或使用用户明确映射的现有资产。CommonUI 按钮不是 `UButton` 的可直接换类版本。
- 页面激活、返回动作、焦点和输入动作依赖项目配置，设计外观本身不确定这些关系。支持明确配置和缺项诊断，不凭空补出业务或输入规则。
- 同一支持清单分别记录“可创建”“样式与状态可承接”“运行前提已满足”“重导入支持”和验证证据，避免把能查找到类当成完整支持。
- 普通 UMG 的 48 种接入及其中 32 种尽量自动识别的目标保留；本节的 CommonUI 32 个生成目标是另一份清单，其中第二类 9 个保留尽量自动识别目标，不用同一个“32”混指两套范围。
- `PostBufferUpdate` 不是 CommonUI 控件，继续归入普通 UMG 第三类，严格仅用户显式标注时转换，并检查目标引擎可用性。

调研依据：上述本机 UE 5.7.1 / UE 5.0.3 CommonUI 源码，以及 [Epic CommonUI 入门指南（5.7）](https://dev.epicgames.com/documentation/en-us/unreal-engine/common-ui-quickstart-guide-for-unreal-engine?application_version=5.7)、[设计指南（5.7）](https://dev.epicgames.com/documentation/en-us/unreal-engine/design-guidelines-for-using-commonui-in-unreal-engine?application_version=5.7)、[CommonRotator 接口说明](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/CommonUI/UCommonRotator)。分类是本项目依据调研作出的设计判断，不是文档给出的识别准确率。

## 4. 信号

### 4.1 两种来源实际有哪些信息

本节保留前次调研记录：在 Pencil 中打开的 `pencil-halo.pen` 设计系统（schema 2.21）、两份真实 Figma 导出（`Examples/AiHud/design.json`、`FigmaRealDashboard.json`），以及当时查阅的 Figma 官方 REST 与 Plugin API 文档。本次未重新联网核实 API；旧样本中的出现情况不代表所有设计稿，具体字段可用性和映射仍需在实现前确认。

**Figma**

| 信息 | 内容 | 实测 |
| --- | --- | --- |
| 组件属性 | `componentPropertyDefinitions`，类型为 `BOOLEAN`、`INSTANCE_SWAP`、`TEXT`、`VARIANT`、`SLOT`；`VARIANT` 带 `variantOptions` | AiHud 的组件集有 `State: Idle / Focus` 等 |
| 实例当前值 | 实例上的 `componentProperties` | AiHud 有 10 个实例带 `State` 值 |
| 原型交互 | `interactions` = 触发 + 动作。触发：`ON_CLICK`、`ON_HOVER`、`ON_PRESS`、`ON_DRAG`、`AFTER_TIMEOUT`、`MOUSE_ENTER/LEAVE/UP/DOWN`、`ON_KEY_DOWN/UP`、`ON_MEDIA_END`。动作：`NODE`（`NAVIGATE` / `OVERLAY` / `SWAP` / `SCROLL_TO` / `CHANGE_TO`）、`BACK`、`CLOSE`、`URL`、`SET_VARIABLE`、`SET_VARIABLE_MODE`、`CONDITIONAL` | AiHud 有 15 处 `ON_CLICK` 时 `CHANGE_TO` 变体，即交互式组件 |
| 滚动 | `overflowDirection` | 已在使用 |
| 变量绑定 | `boundVariables` | 两份导出中都有 |
| 组件描述 | 组件和组件集的 `description` 自由文本 | AiHud 的设计者在描述里写了意图，比如“变体是视觉状态” |
| 标注 | `annotations`（`label` + `properties`，目前最多 1 条） | 两份导出中都没有 |
| 插件数据 | `sharedPluginData`，REST 需要额外请求参数 | 两份导出中都没有 |
| 插槽 | `SLOT` 属性：Plugin API 已于 2026-06 正式发布 `SlotNode`；REST 的属性类型枚举已包含 `SLOT`，但 REST 中节点级的 `SLOT` 表示还没有文档 | 未实测，需要由导出插件补齐 |

**Pencil**

| 信息 | 内容 | 实测（halo 设计系统） |
| --- | --- | --- |
| 变体 / 状态 / 原型交互 | **没有**。schema 中不存在这些概念 | — |
| 组件与实例 | `reusable` 组件 + `ref` 实例，实例可通过 `descendants` 覆盖子节点 | 共 95 个组件 |
| 状态族 | 每个状态是一个独立组件，命名为 `类别/状态`，**并且状态组件之间常常是相互引用的关系** | `Switch/Unchecked` 是 `Switch/Checked` 的 ref；`Accordion/Closed` 是 `Accordion/Open` 的 ref |
| 插槽 | frame 的 `slot: string[]`，列出推荐放入的组件 | `Tabs` 推荐 `Tab Item/Active` 与 `Tab Item/Inactive`；`Dropdown` 推荐列表项组件 |
| 自定义元数据 | 任意节点可带 `metadata: {type, …}` | 原 halo 样本未使用；后续样本中有其他用途，D2U 标注需明确命名空间 |
| 意图文本 | 任意节点可带 `context` 字符串 | 原 halo 样本未使用；后续样本中也有备注用途，不默认等同于控件语义 |
| 图标 | `icon` 节点，带图标库和图标名 | `check`、`chevron-up`、`plus` 等 |
| 链接 | 文本的 `href` | — |
| `placeholder` | “正在编辑”的工作标记，**不是**输入框占位符 | 不作为输入框类型或 HintText 的依据 |

两者的差异：Figma 可提供结构化状态和交互；Pencil 的状态族主要来自命名约定、组件引用与结构差异，部分样本另有自定义元数据。源工具具备字段不代表某份稿子实际填写，也不直接决定其控件类型；不统一规定 Pencil 必须比 Figma 低一档。

### 4.2 显式标注与自动识别证据

| 来源 | Figma | Pencil |
| --- | --- | --- |
| 显式类型标注 | 名称标记 `[d2u:CheckBox]`；计划由导出插件承接的 `design2umg.widget` 数据 | 名称标记；计划采用命名空间明确的 `metadata` 标注 |
| 部件标注 | `[d2u:part=thumb]` 等，写在实际部件上 | 同左 |
| 状态与交互 | 实际填写的变体、布尔属性、状态差异与原型动作 | 组件状态族、引用和实例覆盖差异；已有自定义元数据需逐项核实含义 |
| 结构与几何 | 容器、文字、图标、轨道、填充、滑块等的组合及相对关系 | 同左 |
| 名称与上下文 | 当前节点、父分组、子部件的名称，以及组件关系 | 同左 |

- 显式标注是指令，普通名字是证据。标记在生成控件名时去掉；类型已明确且部件清楚时不要求用户重复标注部件。
- 不以名称含 `Button` / `btn` 作为自动识别的必要条件，也不因出现关键词就直接转换。`thumb`、`track` 等可能命名在子部件，控件语义也可能位于外层分组。
- 保留纯结构识别路径，但由具体控件规则的独立验证决定何时应用，不把“所有纯结构都自动转换”或“所有纯结构永远只列候选”定成全局规则。
- `ON_CLICK`、`Selected`、`BOOLEAN`、插槽推荐信息等都不是单独确定某个控件类的通行证；需要检查实际关系、结构及相互冲突的解释。
- 名字、状态、交互、结构如何组合，哪些词需要拆分及本地化、是否提供可编辑词表，均留待识别方案讨论，不按具体套件、字体、case 名或节点 ID 写分支。
- Figma `sharedPluginData` 的请求方式和本地导出链承接在实现前核实；不假定现有导出插件已经支持 D2U 标注界面。

### 4.3 结果与默认应用条件

识别结果至少说明目标语义、必要部件及依据；生成目标再选择普通 UMG 或 CommonUI。识别出一个候选，不等于该类适配已完成，也不等于运行前提已满足。

| 情况 | 处理原则 |
| --- | --- |
| 用户显式标注 | 按指定类型处理；类型、部件、项目配置或引擎能力缺失时明确报告未完成内容 |
| 自动识别且适配、前提均满足 | 在该控件已验证并启用的规则下应用，记录规则和证据 |
| 证据有歧义或应用前提缺失 | 保留视觉内容并报告候选、冲突或缺少的信息，不静默选一种解释 |
| 用户否决 | `[d2u:none]` 排除相应节点及其子树的自动识别 |

关闭识别、仅显式标注、自动识别及按控件关闭的使用需求保留；具体档位、默认值、是否分置信度以及逐项接受界面留待讨论。前两类 32 种尽量自动识别并转换的目标不变，不用统一的“中”档或虚构准确率替代逐类判断。

既有探索实验以名字作参考，只能说明名字与结构的重合情况，不是准确率。后续使用人工独立确认的类型、部件和必要反例；样本是否代表游戏 UI 单独说明，不把名字同时作为识别证据和标准答案，也不将“确认一个，同款全转”视作已确定规则。

## 5. 生成：从视觉树到控件

标注和自动识别共用控件适配路径，负责把视觉树翻译成控件部件、样式、初始值及自身状态行为。实现职责如下：

1. 解析器保留真实源数据，处理 Figma / Pencil 表达差异，不把未经证实的推断写成源事实。
2. 识别部分确定控件语义及部件；保留源节点身份和必要组件关系，不在此直接生成 UObject。
3. 转换器组织目标控件树，通过现有 UMG 中间数据承接新增的明确类型字段。
4. 编辑器生成部分调用目标引擎接口，设置样式、数值，生成必要的蓝图和样式资产。
5. 序列化、反序列化和重导入同步承接新增字段，复用现有模块，不先建立规则语言或通用插件式识别平台。

| 控件 | 样式来源 | 初始值来源 |
| --- | --- | --- |
| `Button` | Normal / Hovered / Pressed / Disabled 各取对应变体 | — |
| `CheckBox` | 选中 / 未选中变体 | 实例当前的变体值 |
| `Slider` / `ProgressBar` | 轨道、填充、滑块节点 | 几何比例 |
| `EditableTextBox` | 容器外观 + 已明确区分的占位文本、输入内容及字体颜色 | 依据源信息区分 `HintText` 与初始文本，不凭弱化颜色直接当占位文本 |

样式和行为优先使用原生控件自身支持的画刷、文字、图片、内容及状态能力，复用已有资源生成链。复杂外观按实际部件能力处理，必要的生成蓝图连接作为明确适配内容设计，不用“放进 WidgetSwitcher”代替事件、状态和生命周期的完整方案。

静态默认态子树不替代动态部件。例如把对勾作为一直显示的内容塞进 `CheckBox`，即使默认截图相同，取消勾选后仍显示对勾也属于行为错误。`Slider` 的部件和样式同样需要核对原生表达能力，不假定所有源外观都可直接无损承接。

标注确定类型，但不保证缺少的样式和配置凭空补齐。适配缺口使用 `semantic_style_degraded` 或具体缺项诊断，写明缺少的部件、状态或配置，不把 UE 默认外观算成已承接设计。复杂多状态样式的具体生成方式后续细化，目标范围继续保留。

**生成前校验**：只检查该控件在真实路径上必需的部件和前提。自动候选适配失败时保留普通视觉内容并输出 `semantic_rejected`；显式标注失败则明确列为未完成转换，不静默忽略标注。约束示例：

- `Slider` 必须恰好有一个可识别的滑块。
- `Button` 内不能嵌套另一个 `Button`。
- `ProgressBar` 的填充比例必须在 0 到 1 之间。

## 6. 识别、行为与视觉分别验证

默认态截图相同只说明外观接近，不证明语义正确。标签误转成按钮也可能得到同一截图；视觉指标不代替控件类型、部件或行为判断。

1. **识别正确性**：使用人工独立确认的小样本、必要反例核对类型和部件；普通名称不作标准答案。代表性不足的既有数据只提供探索证据，不冒充目标用户准确率。
2. **功能与行为**：复用现有测试，补最少的必要字段传递、序列化、状态变化、事件和重导入检查；验证实际行为，不按布局和填充组合扩张测试矩阵。
3. **视觉与成绩**：使用项目既有 81-case 完整回归，保持同引擎、同源、同有效参考身份；逐 case 核对 `no_regression`、历史最佳和各项指标。任何退步按项目规则报告并处理，不用平均收益抵消单项退步。
4. **控制与诊断**：支持显式否决和关闭识别；导入报告记录 `semantic_applied` / `semantic_candidate` / `semantic_rejected` / `semantic_style_degraded` 及必要证据，避免无解释转换或将缺项计为完成。

不新增逐节点“控件版 / 视觉树版”双渲染、独立像素评分或重复评测框架。候选报告复用导入诊断与已有验证入口；是否需要预览确认交互另行讨论。

`Designs_harvest` 的 208 份 `.pen`、1681-case 及其子集仅由用户明确发起运行，不默认纳入开发自测、81-case 回归或交付验证。本次仅更新文档，不启动这些测试，也不刷新设计源、参考图或登记基线。

## 7. 与现有功能的优先级

区分用户明确指定与自动推断，避免用一个优先级序列静默吞掉另一个明确选择：

1. **显式标注**优先于自动识别、按名字的 Widget Class 覆盖及普通生成目标。标注具体 CommonUI 类型时也遵守此原则。
2. **组件映射**同样是项目的明确选择。标注与现有映射一致时复用映射；冲突时明确报告两者，保留已有资产，待用户选择，不默默忽略标注或重写映射资产。具体选择入口留待讨论。
3. **Widget Class 覆盖**与确定的控件语义、内容能力相容时才复用相应结构和样式。与标注冲突时标注优先；与自动识别冲突时报告并尊重明确覆盖，不将它当成已成功应用识别。
4. **自动识别**使用第 4 节后续确定的逐类规则；普通 UMG / CommonUI 生成目标复用语义结果。
5. **原型交互**优先绑定已经生成的适用控件，不再包第二层按钮。CommonUI 使用其实际按钮事件；其他交互控件与原型动作矛盾时报告冲突，不静默丢弃连线或改变控件类型。
6. **普通视觉转换**用于未产生可应用识别结果的普通节点；显式标注缺项按未完成项报告，而非悄悄当普通节点成功。

**组件蓝图**：复用现有组件定义、实例和变体关系，按实际语义决定是否共用组件蓝图；各实例仍需承接自身属性和状态。普通 Button 与 CommonUI Button 的生成形态不同，后者需要派生控件蓝图和内容树。仅有 Default / Hover 等变体名不直接决定组件集就是按钮。

## 8. 与安全重导入的配合

### 8.1 复用现有记录，补足实际缺口

现有 `.d2u.json` 的实例 `kind` 区分 `mapped` / `generated`，并不表示 Button / CheckBox 等控件类型。现有 KindChanged 判定也首先区分映射与生成；不假定它已经完整处理任意控件类型变化。

在现有记录上补充：

- 已采用的控件语义和实际生成类型，包括普通 UMG / CommonUI 目标。
- 需要稳定跟踪的部件源节点身份。
- 上次生成的受管理属性值，用于区分后续设计改动与用户手改。
- 解释类型变化所需的规则和依据；确有用途时记录规则版本，不预先要求通用信号哈希或独立状态系统。

具体字段与现有 manifest、结构核对及序列化共同设计，不覆盖现有 `kind` 含义，不扩张为旧字段双读、静默迁移或多套旧识别逻辑。

### 8.2 类型、规则和否决变化

| 情况 | 处理 |
| --- | --- |
| 控件类型及生成目标不变 | 按第 8.3 节核对实际受管理属性和用户修改，不无条件刷新所有值 |
| 设计或显式标注要求换类、切换 UMG / CommonUI | 复用现有资产保护、结构核对和冲突诊断，补齐控件类变化的判断；已有用户绑定、引用或内容受到影响时保留资产并报告冲突 |
| 插件规则更新产生不同类型 | 保留已采用结果并明确提示，用户选择重新评估后再走类型变化检查；不为此运行多套旧算法 |
| 用户新增否决标注 | 停止对应范围的自动识别；若恢复普通视觉树会改动已有类型或结构，先执行同样的用户修改保护 |

插件升级本身不应静默改变用户已有控件类型。重新评估入口、诊断代码和需要保存的规则信息留待实现设计核对，不把尚不存在的命令行参数写成现有能力。

### 8.3 属性更新与用户修改

建议对确由设计管理的样式、字体、`HintText`、`CheckedState`、`Value`、`Percent` 等属性，比较上次生成值、新设计生成值和当前资产值：

| 变化 | 处理 |
| --- | --- |
| 仅设计变化，当前仍是上次生成值 | 更新为新设计值 |
| 仅用户修改，设计值未变化 | 保留当前资产值 |
| 双方修改后结果一致 | 保留共同结果 |
| 双方修改且结果不同 | 报告属性冲突，不静默覆盖 |

未由设计提供的属性不强行纳入管理；事件绑定、项目数据和输入动作仍属于项目明确配置。初始值是否另设“仅首次写入”策略以及冲突选择入口列为待决项，不把同一属性同时声明为 D2U 和项目独占。

## 9. 实施顺序与验收

按控件逐组打通“标注 → 部件与样式 → 状态行为 → 自动识别 → 重导入”，不先堆完整规则框架。每组均核对目标引擎差异；仅标注的类型不添加自动识别。分组是实现顺序，全部控件接入、CommonUI 和 32 种尽量自动识别的目标继续有效。

| 顺序 | 内容 | 验收关注点 |
| --- | --- | --- |
| 第一组 | 标注及必要中间字段；`Button`、`CheckBox`、`ProgressBar`、`Slider`；同时打通普通 Button 和 CommonUI Button 两条生成路径 | 实际样式和状态可用；抽象 CommonUI 基类经派生资产生成；原型按钮不重复包装；用户修改可保留 |
| 第二组 | 输入框、`ComboBoxString`、`SpinBox`、`ExpandableArea` 及对应 CommonUI 类型 | 文字、选项、数值、展开等必要行为与配置缺项诊断 |
| 第三组 | 页签、列表、树、动态条目、加载与提示等剩余能力及对应 CommonUI 类型 | 组件关系、条目接口、数据和输入前提明确，不把运行时空内容视为完成 |
| 持续补齐并核对全量 | 其余布局、包装、平台及 CommonUI 控件；`PostBufferUpdate` 等仅标注项 | 按完整清单核对每类支持方式、版本和未覆盖项，不以代表项或分组结果宣称全部完成 |

代表验收场景：显式标注的 Slider 承接轨道、滑块和初始值；按钮没有典型名字也保留综合识别路径；CheckBox 切换后勾选外观随状态变化；CommonUI 按钮生成可用派生资产；低版本的 `PostBufferUpdate` 标注明确报告未完成而不换类；重导入分别处理设计变化、用户手改和冲突。

识别、行为与视觉证据按第 6 节分别取得。影响成绩路径的交付仍执行项目完整 81-case、逐项退步和历史最佳规则；局部测试不替代完整验收。未验证的控件、版本、识别规则和运行前提逐项列出，不把计划或源码存在性当成运行证据。

## 10. 后续讨论与待决事项

已明确保留：48 种普通 UMG 全部接入、CommonUI 32 个生成目标全部接入、普通 UMG 前两类 32 种尽量自动识别、CommonUI 第二类 9 个尽量自动识别、用户显式标注优先、`PostBufferUpdate` 仅标注转换及按引擎版本核对支持。本次仅写方案，不开发。

后续先讨论“怎么识别”，不在本次文档更新中替用户确定：

1. 名称、父子结构、部件、状态、交互分别如何参与提名、排除和确认；无典型名称时如何扩大识别覆盖。
2. 各控件自动应用条件、歧义处理、纯结构规则的验证、默认档位及人工确认入口。
3. 代表性样本及独立人工判断的来源；误判、漏判和覆盖面的实际口径。大规模 Pencil 集的运行仍由用户另行明确发起。
4. 标注沿用 `[d2u:类型]`、`[d2u:part=部件]`、`[d2u:none]` 的提案写法；元数据字段及 Figma 标注界面的具体实现待确定。
5. 复杂多状态外观的生成方式、CommonUI 全量清单对应的分版本属性与输入配置、映射冲突和属性冲突的选择入口。
6. 初始值是否采用仅首次写入策略，以及重新评估识别结果的用户入口。

## 附录：同类产品的做法（2026-10 调研）

以下保留前次调研，未在本次文档更新中重新联网验证；用于说明候选思路，不作为已确认需求、当前产品能力或独有能力的证明。

| | UI Widget Builder | Widget Semantic Bridge | Figma2UMG |
| --- | --- | --- | --- |
| 发布 | Fab，论坛帖 2026-06-30 | Fab，论坛帖 2026-04-15 | 2024 上架，2025-07 开源 |
| 输入 | Photoshop 图层，经 JSX 导出为 `layout.json` + 贴图 | AI 代理编写的 `.widgetdsl` 文本 | Figma REST |
| 控件类型如何确定 | **全靠图层名前缀**，不做推断。如 `BTN_`、`CHK_`、`SLD_`、`PGB_`、`CMB_`、`SPN_`、`ETB_` | AI 直接写类型名 | 组件集变体名为 Default / Hovered / Pressed / Disabled 时转 `UButton`，其他变体转 `WidgetSwitcher` |
| 覆盖范围 | 约 30 种前缀，含 `RadialSlider`（AdvancedWidgets 模块，不在 UMG 的 48 种内） | 白名单 47 种原生控件（48 种中除 `PostBufferUpdate`），每种控件列出全部可写属性和各状态画刷 | 未公开完整清单 |
| 复合控件 | 子部件前缀：`BAR_` 轨道、`THM_` 滑块头、`FILL_` 填充、`BOX_` / `CHKMARK_` 勾选框、`ARW_` 下拉箭头、`OPT_` 选项；按钮状态图 `IMG_Normal/Hover/Pressed` | 由 DSL 属性直接表达 | — |
| 重导入 | 只有“清空后重新生成”，会丢失手工修改 | 只导入新增或变化的文件；同名同类控件保留未写出的属性，被省略的控件删除，改名或改类的控件替换 | Patch 合并 |
| 其他 | 可选生成逻辑：页签切换、按 Esc 关闭、音效、单选组 `CHKG_`；UE 5.6–5.8 | 可把现有 WBP 导出为 DSL，导入前校验并生成预览图；反射属性兜底，需可无损往返 | 字体缺失时可从 Google Fonts 下载 |

可借鉴：

1. **子部件标注**：复杂控件除了标注控件本身，还要能标注部件（轨道、滑块头、填充、勾选标记、下拉箭头、选项）。本方案的标注语法应支持 `[d2u:Slider]` 加 `[d2u:part=thumb]` 这一层，自动识别找不准部件时，设计者可以直接指明。
2. **单选组**：前次调研记录 UI Widget Builder 用 `CHKG_` 表示互斥组。组标注及互斥逻辑只保留为后续候选，未计入本次默认生成范围。
3. **按控件的属性白名单**：Widget Semantic Bridge 为 47 种控件逐一列出可写属性和各状态画刷。第 5 节的适配器需要同样完整的清单，才能做到 48 种全部接入。
4. **可选的辅助逻辑**：页签切换、关闭键、音效等，可以作为独立开关的生成选项。它们不属于识别本身，可另行规划。

本方案关注设计数据驱动的控件生成、自动识别和用户修改保护。竞品是否具有相同能力需要独立核实，不据旧调研宣称独有；视觉回归与语义正确性分别验证。
