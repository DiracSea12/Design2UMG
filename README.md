# PenToUMG

将 Pencil 设计文件（.pen）转换为 UE5 UMG Widget Blueprint。

## 功能

- **文件导入**：将 .pen 文件拖入 Content Browser，自动生成 Widget Blueprint
- **实时连接**：通过 MCP 协议连接 Pencil 编辑器，实时同步设计到 UE5
- **预览窗口**：生成前可预览和编辑中间 JSON 结构
- **远程图片**：自动下载设计中引用的远程图片并导入为 Texture2D

## 环境要求

- Unreal Engine 5.1+
- Pencil 桌面应用（实时连接功能需要）

## 设置

**路径**: `Edit → Editor Preferences → Plugins → Pen To UMG`

### Generation Settings

| 设置项 | 说明 | 默认值 |
|--------|------|--------|
| Design Width | UMG 设计分辨率宽度 | 1920 |
| Design Height | UMG 设计分辨率高度 | 1080 |

### Remote Image Import

| 设置项 | 说明 | 默认值 |
|--------|------|--------|
| Enable Remote Image Import | 是否自动下载远程图片 | true |
| Remote Image Import Path | 导入图片的保存路径 | /Game/PenToUMG/ImportedImages |
| Remote Image Timeout | 下载超时（秒） | 20 |

### Pencil Connection

| 设置项 | 说明 | 默认值 |
|--------|------|--------|
| MCP Server Path | MCP 服务器路径（留空自动发现） | 空 |
| App Name | 应用名称（留空自动检测） | 空 |
