# Design2UMG - Figma 本地导出导入指南

## 概述

当 Figma REST API 不可用时（如免费账户配额耗尽），可通过本地导出方式将 Figma 设计稿导入 UE5。

流程：**Figma 桌面版导出 → 本地文件 → UE5 编辑器导入**

## 前置条件

- Figma 桌面版（非浏览器版）
- UE5 编辑器 + Design2UMG 插件已编译

## Step 1：安装 Figma Plugin

1. 打开 Figma 桌面版
2. 菜单 → **Plugins** → **Development** → **Import plugin from manifest...**
3. 选择本仓库里的 `manifest.json`：

```
<PluginRoot>\Tools\FigmaExporter\manifest.json
```

安装完成后，插件会出现在 Development 插件列表中。

## Step 2：从 Figma 导出

1. 在 Figma 中选中要导出的 **Frame**（或 Component / Instance）
2. 右键 → **Plugins** → **Development** → **Design2UMG Exporter**
3. 设置 **Scale**（图片缩放倍数，默认 2x，推荐保持）
4. 点击 **Export Selected Frame**
5. 自动下载单个 `figma_export.zip` 文件

导出的 ZIP 内容：

```
figma_export.zip
├── design.json              # REST API 兼容的设计数据
├── reference.png            # Figma 对选中根节点的官方 PNG 渲染（与 design.json 同次导出）
└── images/
    ├── {imageHash}.png      # 图片填充素材
    └── vec_{nodeId}.png     # 矢量节点渲染图
```

## Step 3：在 UE5 中导入

1. 解压 `figma_export.zip` 到任意目录
2. 打开 UE5 编辑器
3. 菜单栏 → **Design2UMG** → **Import Local Figma Export...**
4. 在文件对话框中选择解压出的 `design.json`
4. 插件会自动：
   - 解析 JSON 设计数据
   - 扫描同级 `images/` 目录匹配图片
   - 将图片导入为 UE5 纹理资产，保存目录由 `Remote Image Import Path` 设置决定，默认 `/Game/Design2UMG/ImportedImages`
   - 打开预览窗口展示转换结果

## 注意事项

- **图片目录必须与 design.json 同级**，且命名为 `images/`
- 图片文件名必须是 `{imageRef}.png` 格式（插件自动生成，无需手动修改）
- 重复导入同一设计时，已存在的纹理资产会被复用，不会重复创建
- 矢量节点（VECTOR、STAR、LINE 等）会被自动渲染为 PNG 导出
- `reference.png` 由 Figma Plugin API 对选中的根节点直接渲染，用作该导出的权威参考图；不会通过 JSON 或其他图片后处理补背景

## 故障排查

| 问题 | 原因 | 解决 |
|------|------|------|
| 导出后没有 images 目录 | 设计中没有图片填充或矢量节点 | 正常现象，纯色/渐变不需要图片 |
| UE5 导入后图片缺失 | images/ 目录不在 design.json 同级 | 确保目录结构正确 |
| 解析失败 | JSON 格式损坏 | 重新从 Figma 导出 |
| Plugin 不在菜单中 | manifest 未正确导入 | 重新 Import plugin from manifest |

## 技术细节

### 属性映射

Figma Plugin API 与 REST API 的字段名有差异，插件自动处理了以下映射：

| Figma Plugin API | 导出 JSON（REST API 格式） |
|---|---|
| `fills[i].imageHash` | `fills[i].imageRef` |
| `node.textAutoResize` | `textAutoResize`（节点根级别） |
| `node.fontSize/fontName/...` | `style: { fontSize, fontFamily, lineHeightUnit, ... }` |
| `node.strokeTopWeight` 等 | `individualStrokeWeights: { top, right, bottom, left }` |
| `node.mainComponent.id` | `componentId` |

### 文件位置

```
Design2UMG/
├── Source/Design2UMGEditor/Private/
│   ├── PenRemoteImageImporter.cpp    # 本地文件路径支持
│   └── Design2UMGEditorModule.cpp    # 本地导入入口 + 菜单
└── Tools/FigmaExporter/
    ├── manifest.json                  # Figma 插件清单
    ├── code.ts                        # 节点序列化 + 图片导出
    └── ui.html                        # 导出 UI
```
