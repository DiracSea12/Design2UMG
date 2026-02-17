# Design2UMG Property Mapping Reference

.pen 节点属性到 UMG Widget 属性的完整映射关系。

## Node Type → Widget Type

| .pen Type | UMG Widget | Condition |
|-----------|-----------|-----------|
| Frame | VerticalBox | Layout == Vertical |
| Frame | HorizontalBox | Layout == Horizontal |
| Frame | CanvasPanel | Layout == None |
| Rectangle | Border | |
| Ellipse | Border | ⚠️ 降级为矩形 |
| Polygon | Border | ⚠️ 降级为矩形 |
| Path | Image | ✅ SVG 路径光栅化 |
| Text | TextBlock | |
| IconFont | TextBlock | |
| Line | Image | |
| Group | CanvasPanel | |

## Text Properties

| .pen | UMG TextBlock | Notes |
|------|--------------|-------|
| Content | Text | FText::FromString |
| FontSize | Font.Size | DisplaySize × FontDPI / RenderDPI |
| FontFamily | Font.FontObject / TypefaceFontName | 资产路径 → LoadObject; 名称 → FName |
| FontWeight | Font.TypefaceFontName | 见下方权重映射表 |
| LetterSpacing | Font.LetterSpacing | tracking = pixelSpacing / fontSize × 1000 |
| TextAlign | Justification | Left / Center / Right / Justify |
| TextAlignVertical | VerticalAlignment | Top / Middle / Bottom |
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
| Gradient (Linear H) | MaterialRef = "gradienth" | 角度 0-45° 或 225-315° |
| Gradient (Linear V) | MaterialRef = "gradientv" | 角度 45-135° 或 315-45° |
| Gradient (Radial/Angular) | MaterialRef = "radialgradient" | |
| Image | TexturePath | URL 远程下载或本地路径 |

⚠️ **部分支持**: 渐变仅支持双色（ColorStart/ColorEnd），不支持多色停靠点。

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
| CornerRadius | Brush.OutlineSettings | 支持单值或四角独立 |
| Stroke.Color | Brush.OutlineColor | |
| Stroke.Thickness | Brush.OutlineThickness | |

## Effects

| .pen Effect | UMG | Support |
|------------|-----|---------|
| Shadow (outer) | BoxShadow 包裹 | ✅ Offset, Blur, Spread, Color |
| Shadow (inner) | BoxShadow 包裹 | ✅ bIsInner |
| BackgroundBlur | BackgroundBlur 包裹 | ✅ BlurStrength |
| Blur | — | ❌ 不支持（需自定义 Material） |

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

## Unsupported Properties

| Property | Reason |
|----------|--------|
| Blur effect | 需要自定义 Material 实现 |
| Multi-stop gradient | 仅支持双色渐变 |
| Complex polygon shapes | 降级为矩形 Border |
| Ellipse arc (StartAngle/SweepAngle) | 降级为矩形 |
| Line dash patterns | 不支持 |
| FlipX / FlipY | RenderTransformScale = -1 | ✅ 通过缩放实现 |
