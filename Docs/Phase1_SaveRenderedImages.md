# Phase1 测评 - 保存渲染图片功能

## 概述

在 Phase1 测评过程中，系统会渲染 Widget 生成 PNG 图片用于 AI 分析。现在你可以选择将这些渲染的图片保存到本地，方便检查和调试。

## 使用方法

### 1. BatchTest 命令

```bash
ImageToUMG.BatchTest <ContentPath> --save-images [--images-path=<path>]
```

**参数说明：**
- `--save-images`: 启用保存渲染图片功能
- `--images-path=<path>`: （可选）指定保存路径，默认为 `Saved/ImageToUMG/RenderedImages`

**示例：**

```bash
# 使用默认路径保存图片
ImageToUMG.BatchTest /Game/UI --save-images

# 指定自定义保存路径
ImageToUMG.BatchTest /Game/UI --save-images --images-path=D:/MyImages

# 结合其他参数使用
ImageToUMG.BatchTest /Game/UI --config=v2_full --save-images --output=Saved/Reports
```

### 2. AutoEval 命令

```bash
ImageToUMG.AutoEval <ContentPath> --save-images [--images-path=<path>]
```

**示例：**

```bash
# 自动评测并保存图片
ImageToUMG.AutoEval /Game/UI --save-images

# 消融实验并保存图片
ImageToUMG.AutoEval /Game/UI --ab-matrix --save-images
```

## 输出文件

### 文件命名规则

- 单次迭代：`WidgetName.png`
- 多次迭代：`WidgetName_Iter1.png`, `WidgetName_Iter2.png`, ...

### 默认保存路径

```
<ProjectDir>/Saved/ImageToUMG/RenderedImages/
├── WBP_HUD_Main.png
├── WBP_HealthBar.png
├── WBP_Minimap.png
└── ...
```

## 注意事项

1. **磁盘空间**：保存图片会占用磁盘空间，建议定期清理旧图片
2. **性能影响**：保存图片会略微增加测评时间（每张图片约 1-10ms）
3. **路径权限**：确保指定的保存路径有写入权限
4. **文件覆盖**：同名文件会被覆盖，建议使用不同的输出路径区分不同测试

## 使用场景

### 1. 调试 AI 识别问题

当 AI 识别结果不准确时，查看渲染的图片可以帮助你：
- 确认 Widget 是否正确渲染
- 检查图片质量是否足够清晰
- 验证元素是否在预期位置

### 2. 验证测试集质量

保存图片后可以：
- 人工检查测试样本是否合理
- 发现渲染异常的 Widget
- 评估测试集的多样性

### 3. 制作文档和报告

保存的图片可以用于：
- 制作测评报告的配图
- 展示优化前后的对比
- 记录问题案例

## 示例工作流

### 完整的测评流程（带图片保存）

```bash
# 1. 运行测评并保存图片
ImageToUMG.BatchTest /Game/UI --config=v2_full --save-images

# 2. 检查保存的图片
# 打开 Saved/ImageToUMG/RenderedImages/ 目录

# 3. 查看测评报告
# 打开 Saved/ImageToUMG/Reports/AutoEval_<timestamp>.json

# 4. 如果发现问题，可以对比图片和报告数据进行调试
```

### 对比不同配置的渲染结果

```bash
# 配置 A：保存到 ImagesA 目录
ImageToUMG.BatchTest /Game/UI --config=v2_full --save-images --images-path=Saved/ImagesA

# 配置 B：保存到 ImagesB 目录
ImageToUMG.BatchTest /Game/UI --config=v2_no_fixer --save-images --images-path=Saved/ImagesB

# 然后对比两个目录中的图片
```

## 技术细节

### 图片格式

- **格式**：PNG（无损压缩）
- **色彩空间**：RGBA（支持透明度）
- **分辨率**：与 DesignSize 或 --viewport 参数指定的分辨率一致

### 渲染流程

1. 创建 Widget 实例
2. 渲染到 RenderTarget
3. 读取像素数据
4. 编码为 PNG 格式
5. 保存到磁盘（如果启用 --save-images）
6. 发送给 AI 分析

### 性能优化

- 使用异步文件写入（不阻塞主线程）
- 批量创建目录（避免重复检查）
- 复用 PNG 编码器（减少内存分配）

## 常见问题

### Q: 图片保存失败怎么办？

**A:** 检查以下几点：
1. 确保保存路径有写入权限
2. 检查磁盘空间是否充足
3. 查看 UE 日志中的错误信息

### Q: 可以只保存失败的样本吗？

**A:** 当前版本会保存所有样本。如果只需要失败样本，可以：
1. 先运行测评生成报告
2. 从报告中找到失败的样本名称
3. 手动筛选对应的图片文件

### Q: 图片质量如何调整？

**A:** 当前使用 PNG 无损格式，质量已经是最高的。如果需要调整分辨率，可以使用 `--viewport` 参数：

```bash
ImageToUMG.BatchTest /Game/UI --save-images --viewport=2560x1440
```

## 相关文档

- [Phase1_OptimizationEvaluation.md](Phase1_OptimizationEvaluation.md) - 完整的测评方案
- [TechnicalDesign_v2.2.md](TechnicalDesign_v2.2.md) - 技术设计文档
- [DevelopmentProgress.md](DevelopmentProgress.md) - 开发进度跟踪
