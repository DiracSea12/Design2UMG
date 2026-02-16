# Design2UMG 已知限制

降级渲染三方案（Ellipse/Path/BoxShadow）的已知限制，暂不修复但需跟踪。

## Path 栅格化

### ~~fill:none + stroke-only 的 Path 会被填白色~~ [已修复]
- **文件**: `Design2UMGConverter.cpp` `CreatePathWidget()`
- **状态**: ✅ 已修复。`Fill.Type != Color` 时跳过栅格化走 fallback，不再硬编码 `#ffffffff`

### ~~SVG S/s、T/t 指令未支持~~ [已修复]
- **文件**: `PenPathRasterizer.cpp` `ParsePathData()`
- **状态**: ✅ 已修复。新增 S（smooth cubic）和 T（smooth quad）case 分支，通过 LastControl 镜像上一个控制点实现平滑曲线

### ~~Path 栅格化无缓存，每次重建 UTexture2D~~ [已修复]
- **文件**: `PenPathRasterizer.cpp` `Rasterize()`
- **状态**: ✅ 已修复。新增 WeakPtr 缓存，以 `Geometry + Width + Height + FillColor + FillRule` 的 uint32 hash 为键，命中直接复用

### Path 缓存使用 uint32 hash 做 key，理论上存在碰撞风险
- **文件**: `PenPathRasterizer.cpp` `MakeCacheKey()`
- **现象**: 不同参数组合可能产生相同的 uint32 hash，碰撞时会返回错误纹理而非 cache miss
- **影响**: 概率极低（缓存上限 256 条，单次碰撞概率 ≈ 6e-8），编辑器场景下几乎不可能触发
- **修复方向**: 如需消除可改用结构体 key（需解决匿名命名空间 GetTypeHash name hiding 问题，详见 Docs/LessonsLearned.md）

### Rasterize 缩放策略用 min(scaleX, scaleY) 居中，可能留边
- **文件**: `PenPathRasterizer.cpp` `Rasterize()`
- **现象**: 采用等比缩放 + 居中，非正方形 path 在目标尺寸中可能留白边，与设计稿"拉伸填满"预期不一致
- **影响**: 宽高比差异大的 path 会有比例偏差
- **修复方向**: 可选提供 stretch 模式（分别用 scaleX/scaleY），或由上层传入缩放策略

### ~~nonzero 扫描线填充 O(n³) 性能~~ [已修复]
- **文件**: `PenPathRasterizer.cpp` `ScanlineFill()` nonzero 分支
- **状态**: ✅ 已修复。当前实现已是标准 winding 累加 + span 记录方案，O(n) per scanline，无 j/k 双重回溯

## Box Shadow

### Overlay 不扩展尺寸，阴影在 ClipToBounds 下被裁剪
- **文件**: `PenVisualMapper.cpp` `WrapWithBoxShadow()`
- **现象**: Overlay 外层尺寸等于内容尺寸，阴影 Image 通过 RenderTranslation 偏移+更大的 WidthOverride/HeightOverride 实现，但父级 ClipToBounds 会裁掉溢出部分
- **影响**: 紧凑布局或有裁剪的容器中阴影不完整
- **修复方向**: 需要在 Overlay 外再包一层 SizeBox 扩展尺寸，或改用 Slate 层的 OnPaint 自绘阴影

### 只支持单个外阴影 + 均匀圆角
- **文件**: `PenVisualMapper.cpp` `WrapWithBoxShadow()`
- **现象**: 多个 shadow effect 只取第一个；非均匀圆角（四角不同）时只用 Uniform 值
- **影响**: 多层阴影设计和非均匀圆角阴影不准确
- **修复方向**: 多阴影需要多层 Image 叠加；非均匀圆角需要自定义 material

## Ellipse

### ~~非正圆椭圆近似为胶囊形~~ [已修复]
- **文件**: `Design2UMGConverter.cpp` Ellipse case
- **状态**: ✅ 已修复。有尺寸+颜色填充+无描边的椭圆自动生成 SVG path（两段半弧），走 Path 栅格化管线；有描边/无填充/无尺寸时 fallback 到圆角近似（Border 支持 OutlineThickness，Image 不支持）
