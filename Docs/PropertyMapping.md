# Design2UMG Property Mapping Reference

.pen 节点属性到 UMG Widget 属性的完整映射关系。

## Typed Property Status

- 当前常用的 `WidgetStyle / TextData / Brush / Effect / PathData / Render / WidgetState` 字段已进入 typed schema。
- `UMGJson` parsing removes same-name legacy `Properties`; runtime consumers use the typed payload merge directly, with precision preservation only for the few quantization-sensitive typed fields.
- `Properties` 仍保留给尚未 typed 化的 widget-specific 参数；新公共字段不应再继续落回 legacy key。
- 编辑器侧贴图导入缓存已按源身份隔离：本地/远程图片资产使用 `source + content-version` 复合键，本地 Figma 派生图按 `源文档 + 源文件身份` 隔离，避免跨来源串用和共享资产污染。

## Node Type → Widget Type

| .pen Type | UMG Widget | Condition |
|-----------|-----------|-----------|
| Frame | VerticalBox | Layout == Vertical |
| Frame | HorizontalBox | Layout == Horizontal |
| Frame | CanvasPanel | Layout == None |
| Rectangle | Border | |
| Ellipse | Image (SVG) / Border | 有尺寸+纯色填充+无描边 → SVG 光栅化；否则 → 圆角 Border |
| Polygon | Image / Border | 有 PathGeometry → 路径光栅化；否则 → Border 警告降级 |
| Path | Image | ✅ SVG 路径光栅化（有 geometry+颜色时）；Image fill → 直接纹理 |
| Text | TextBlock | |
| IconFont | TextBlock | 连字文本渲染图标 |
| Line | Image | 有 geometry → 路径光栅化；否则自动补水平线 geometry 后光栅化 |
| Group | CanvasPanel | |
| Ref | (展开为引用节点) | 组件实例展开，支持主题覆盖 |

## Text Properties

| .pen | UMG TextBlock | Notes |
|------|--------------|-------|
| Content | Text | 纯文本字符串 |
| FontSize | Font.Size | 像素大小 |
| FontFamily | Font.FontObject | 通过 FFontResolver 解析为资产路径 → LoadObject |
| FontWeight | Font.TypefaceFontName | 见下方权重映射表 |
| LetterSpacing | Font.LetterSpacing | 像素间距 |
| LineHeight | LineHeightPercentage | 百分比乘数 |
| TextAlign | Justification | Left / Center / Right |
| TextAlignVertical | TextAlignVertical + TextBlock Margin | Top / Center / Bottom；固定高文本会转成内部 margin，父 Slot 不动 |
| TextGrowth (Auto) | 默认单行；`justify` 或 `fill_container(N)` 时导出 `AutoWrapText + WrapTextAt` | 保持测量链与运行时换行一致 |
| TextGrowth (FixedWidth) | AutoWrapText=true + WrapTextAt=ResolvedWidth | 固定宽度换行 |
| TextGrowth (FixedWidthHeight) | AutoWrapText=true + WrapTextAt=ResolvedWidth + Clipping=ClipToBounds | 换行+裁剪溢出 |
| Color (from Fill) | ColorAndOpacity | FSlateColor |

### Font Weight Mapping

| .pen | UMG TypefaceFontName |
|------|---------------------|
| 100 / thin | Thin |
| 200 / extralight | ExtraLight |
| 300 / light | Light |
| 400 / normal / regular | Regular |
| 500 / medium | Medium |
| 600 / semibold | SemiBold |
| 700 / bold | Bold |
| 800 / extrabold | ExtraBold |
| 900 / black | Black |

## Fill Properties

| .pen Fill Type | UMG | Notes |
|---------------|-----|-------|
| Color | BrushColor / ColorAndOpacity | Text 用 ColorAndOpacity，其他用 BrushColor |
| Gradient (Linear 任意角度) | MaterialRef = "gradientlinear" | 通过 Rotation + 程序纹理生成 |
| Gradient (Radial) | MaterialRef = "radialgradient" | 支持双色与多色 `Stops`，并透传 `center/size/rotation` |
| Gradient (Angular) | MaterialRef = "angulargradient" | 独立 angular 分支，不再降级为 radial |
| Image | TexturePath | URL 远程下载或本地路径 |

⚠️ **部分支持**: Linear / Radial / Angular 已支持多色停靠点（`Stops`）；Radial 已支持 `center/size/rotation`；`mesh_gradient` 仍不支持。

### Image Mode

| .pen Mode | UMG ImageMode | Notes |
|-----------|--------------|-------|
| Crop | "Crop" | 支持仿射变换 (ImageTransform) |
| Fill | "Fill" | |
| Fit | "Fit" | |

## Layout Properties

| .pen | UMG | Notes |
|------|-----|-------|
| Gap | Slot.Padding | 子元素间距 |
| Padding | Widget.Padding | Left, Top, Right, Bottom |
| JustifyContent | Spacer 插入 | Center / End / SpaceBetween / SpaceAround |
| AlignItems | Slot HAlign/VAlign | Start / Center / End |

**Figma 扩展布局补充：**

| Figma | UMG | Notes |
|------|-----|-------|
| overflowDirection = `VERTICAL_SCROLLING` | ScrollBox + Vertical | 单轴可滚动 Frame 会映射为 `ScrollBox` |
| overflowDirection = `HORIZONTAL_SCROLLING` | ScrollBox + Horizontal | 默认隐藏滚动条，保留内容流布局 |

### Size Mode Mapping

**Vertical 布局:**

| .pen Width | UMG HorizontalAlignment |
|-----------|------------------------|
| FillContainer | Fill |
| Fixed / FitContent | Left |

| .pen Height | UMG SizeRule |
|------------|-------------|
| FillContainer | Fill |
| Fixed / FitContent | Auto |

**Horizontal 布局:**

| .pen Height | UMG VerticalAlignment |
|------------|----------------------|
| FillContainer | Fill |
| Fixed / FitContent | Top |

| .pen Width | UMG SizeRule |
|-----------|-------------|
| FillContainer | Fill |
| Fixed / FitContent | Auto |

**CanvasPanel (绝对定位):**

| .pen Size Mode | UMG | Notes |
|---------------|-----|-------|
| FillContainer | Anchors + Offsets | Min/Max anchors |
| FitContent | bAutoSize = true | |
| Fixed | Offsets (L,T,R,B) | 直接像素值 |

## Visual Properties

| .pen | UMG | Notes |
|------|-----|-------|
| Opacity | RenderOpacity | 与图片 opacity 合并（乘积） |
| Rotation | RenderAngle | 旋转角度 |
| FlipX | RenderTransformScale.X = -1 | 水平翻转 |
| FlipY | RenderTransformScale.Y = -1 | 垂直翻转 |
| Clipping | Clipping=ClipToBounds | 裁剪子元素 |
| CornerRadius (Uniform) | Brush.DrawAs=RoundedBox + OutlineCornerRadius | 超过半尺寸时钳制为药丸形 |
| CornerRadius (Per-corner) | Brush.CornerRadii | "TL,TR,BR,BL" 四角独立 |
| Stroke.Color | Brush.OutlineColor | |
| Stroke.Thickness (Uniform) | Brush.OutlineThickness | 单值 |
| Stroke.Thickness (Per-side) | Brush.OutlineThickness | ⚠️ 取四边最大值（UMG 限制） |
| Stroke.Align | — | ❌ UMG 始终居中对齐 |

## Effects

| .pen Effect | UMG | Support |
|------------|-----|---------|
| Shadow (outer) | BoxShadow 包裹 | ✅ Offset, Blur, Spread, Color |
| Shadow (inner) | RetainerBox + EffectMaterial | ✅ 运行时近似支持（typed `EffectMaterial / TextureParameter / EffectColor / EffectOffset / EffectBlurUVStep`） |
| BackgroundBlur | BackgroundBlur 包裹 | ✅ BlurStrength |
| Blur | RetainerBox + EffectMaterial | ✅ 运行时近似支持（typed `EffectMaterial / TextureParameter / EffectBlurUVStep`） |

## Prototype (Figma)

`Prototype Basic` 是当前正式支持范围，但它的正式口径只覆盖页面切换型交互和基础过渡近似，不覆盖完整设计器原型语义复刻。

| Figma Prototype | UMG | Notes |
|-----------------|-----|-------|
| Top-level multi-page flow | WidgetSwitcher | 顶层 Frame 页面自动收口为 `WidgetSwitcher` |
| `ON_CLICK + NAVIGATE/SWAP` | Button wrapper + runtime binding | 触发节点包装为透明 `Button`，运行时切页 |
| `ON_HOVER / ON_PRESS / ON_DRAG` | Button wrapper + runtime binding | 继续复用透明 `Button` 包裹；hover/press 走原生委托，drag 走原生 drag detect 委托 |
| `ON_KEY_DOWN / ON_KEY_UP` | Typed keyboard binding | 保留触发节点语义，并通过 prototype runtime 的焦点约束输入路由触发 |
| `SET_VARIABLE / SET_VARIABLE_MODE / CONDITIONAL` | Typed step program | 运行时按顺序执行变量写入、条件分支与终端导航动作 |
| `ON_CLICK + OVERLAY` | Button wrapper + typed overlay binding | 运行时把目标页挂到独立 overlay host，并维护 overlay stack；不是 switcher page push 伪切页 |
| `ON_CLICK + BACK` | Button wrapper + typed back binding | 运行时从 prototype history stack 回退上一页 |
| `ON_CLICK + OPEN_LINK` | Button wrapper + typed open-link binding | 运行时直接打开 URL；自动化测试通过 URL capture hook 验证 |
| `DISSOLVE` / `SMART_ANIMATE` / `SCROLL_ANIMATE` | Switcher fade | 映射基础透明度过渡，保留时长 / easing |
| `MOVE_IN` / `MOVE_OUT` / `SLIDE_IN` / `SLIDE_OUT` / `PUSH` | Switcher translation | 映射基础位移过渡，保留方向 / 时长 / easing |

⚠️ **当前边界**: 当前会为切页 fade / slide 生成最小两段式 `UWidgetAnimation` 资产；运行时对 fade 类过渡优先播放资产动画，slide 类过渡继续走 ticker 近似以匹配 live switcher 尺寸。但它仍不生成完整通用动画 authoring 链；overlay 目前虽已进入独立 overlay host contract，但还不是完整浮层系统；也不复刻完整 Smart Animate 图层配对语义。

## Alignment Mapping

**AlignItems in Vertical layout → HAlign:**

| .pen | UMG |
|------|-----|
| Start | HAlign_Left |
| Center | HAlign_Center |
| End | HAlign_Right |

**AlignItems in Horizontal layout → VAlign:**

| .pen | UMG |
|------|-----|
| Start | VAlign_Top |
| Center | VAlign_Center |
| End | VAlign_Bottom |

## Icon Font Properties

| .pen | UMG TextBlock | Notes |
|------|--------------|-------|
| IconFontName | Text (连字文本) | 如 "dashboard" 在 Material Symbols 中渲染为图标 |
| IconFontFamily | FontFamily | 通过 Settings.IconFontAssetMap 解析为资产路径 |
| Weight / FontWeight | TypefaceFontName | 优先用 `fontWeight`，否则回退到 `icon_font.weight` 推断字重 |
| FontSize | FontSize | 图标像素大小 |
| Fill (Color) | Color | 图标颜色 |

说明：Pencil 的 `icon_font.weight` 现在已进入主链，不再只是 parser 保留字段；真实导出会把它映射到 `TypefaceFontName`。

## Unsupported Properties

| Property | Reason |
|----------|--------|
| Legacy `Properties` full removal | Completed for current final consumers: widget appliers, resource binders, and procedural material/image flows now use typed payloads or narrow typed request DTOs instead of a generic property bag |
| Foreground blur full semantic parity | 当前走 `RetainerBox + EffectMaterial` 近似，不是完整设计器语义还原 |
| Inner shadow full semantic parity | 当前为单层 `RetainerBox + EffectMaterial` 近似，不是完整设计器/浏览器语义 |
| Prototype overlay full layer semantics / generalized history graph | 当前 overlay/back 已进入 typed runtime contract，但仍以 `WidgetSwitcher` 页面切换为中心，没有独立浮层系统或通用状态机/事件图生成链 |
| Full `UWidgetAnimation` asset generation | 当前仅为切页 fade / slide 生成最小两段式动画资产；未进入通用动画 authoring / 蓝图图表生成 |
| Font metric full semantic parity | 当前字体解析与文本测量已做缓存隔离和同步，但仍是基于 UE 字体资产/Slate 的启发式近似，不是设计器排版引擎逐像素复刻 |
| Polygon without PathGeometry | 会降级为 Border，并记录 warning |
| `TEXT_PATH` | 当前只保留普通文本 fallback；没有 text-on-path 的 geometry/shaping 渲染链 |
| `mesh_gradient` | 当前渐变 contract 与程序纹理生成器仅覆盖 linear / radial / angular |
| Stroke per-side thickness | ⚠️ 部分支持：取四边最大值 |
| Stroke alignment | UMG 始终居中对齐 |
| Line dash patterns | 不支持 |

## Container Wrapping Strategy

转换过程中，节点可能被额外的容器包裹以实现特定效果：

| 条件 | 包裹控件 | 用途 |
|------|---------|------|
| 有填充/内边距/圆角/描边 | Border | 合并视觉属性 |
| 多层填充 (Fills > 1) | Overlay | 自底向上叠加填充层 |
| 有外阴影效果 | Overlay + Image | 外阴影层渲染在内容下方 |
| 有前景模糊/内阴影 | RetainerBox | 子树后处理效果 |
| 有背景模糊效果 | BackgroundBlur | 模糊背景 |
| 需要固定/最小尺寸 | SizeBox | 强制尺寸约束 |
