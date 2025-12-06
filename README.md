# ImageToUMG 插件使用指南

## 概述

ImageToUMG 是一个 AI 驱动的图片转 UMG Widget Blueprint 生成器。它通过多模态 LLM（GPT-4o/Claude/Gemini）分析 UI 截图，自动生成对应的 Widget Blueprint。

## 配置

### 1. 打开设置面板

**路径**: `Edit → Editor Preferences → Plugins → Image To UMG`

### 2. 配置 API

| 设置项 | 说明 | 默认值 |
|--------|------|--------|
| **API Key** | LLM 服务的 API 密钥（必填） | - |
| **LLM Provider** | 选择 AI 提供商 | OpenAI |
| **Model Name** | 模型名称 | gpt-4o |
| **Max Tokens** | 最大输出 Token 数 | 4096 |
| **Custom Endpoint** | 自定义 API 端点（代理服务器） | 空 |
| **Timeout** | HTTP 请求超时时间（秒） | 120 |

### 3. 配置生成选项

| 设置项 | 说明 | 默认值 |
|--------|------|--------|
| **Default Output Path** | 生成的 Widget Blueprint 保存路径 | /Game/UI/Generated |
| **Auto Open After Generation** | 生成后自动打开编辑器 | true |

### 4. 支持的 LLM 提供商

#### OpenAI
```
API Key: sk-xxx...
Model Name: gpt-4o (推荐) / gpt-4o-mini
```

#### Anthropic (Claude)
```
API Key: sk-ant-xxx...
Model Name: claude-sonnet-4-20250514 (推荐)
```

#### Google (Gemini)
```
API Key: AIza...
Model Name: gemini-1.5-pro (推荐) / gemini-1.5-flash
```

### 5. 使用代理服务器

如果需要通过代理访问 API，在 **Custom Endpoint** 中填入代理地址：
```
https://your-proxy.com/v1/chat/completions
```

## 使用方法

### 方式一：从 Texture2D 资产生成

1. 在 Content Browser 中导入 UI 截图（PNG/JPG）
2. 右键点击导入的 Texture2D 资产
3. 选择 **"Generate UMG Widget"**
4. 等待 AI 分析完成
5. 在预览窗口中查看和编辑生成的 JSON
6. 点击 **"Generate"** 确认生成

### 方式二：预览窗口

生成过程中会弹出预览窗口，显示：
- 原始图片预览
- AI 生成的 JSON 结构
- 可编辑的 JSON 文本框

你可以在生成前手动调整 JSON 内容。

## 支持的 Widget 类型

### 容器类
- CanvasPanel（绝对定位）
- VerticalBox（垂直布局）
- HorizontalBox（水平布局）
- Overlay（层叠布局）
- ScrollBox（滚动容器）
- Border（边框容器）
- SizeBox（尺寸约束）

### 控件类
- TextBlock（文本）
- Button（按钮）
- Image（图片）
- EditableTextBox（单行输入）
- MultiLineEditableTextBox（多行输入）
- CheckBox（复选框）
- Slider（滑块）
- ProgressBar（进度条）
- ComboBox（下拉框）
- Spacer（空白）

## JSON 格式示例

```json
{
  "WidgetName": "WBP_LoginScreen",
  "RootWidget": {
    "Type": "CanvasPanel",
    "Name": "RootCanvas",
    "Children": [
      {
        "Type": "TextBlock",
        "Name": "TitleText",
        "Properties": {
          "Text": "Welcome",
          "FontSize": "24"
        },
        "Slot": {
          "Anchors": { "Min": {"X": 0.5, "Y": 0.1}, "Max": {"X": 0.5, "Y": 0.1} },
          "Alignment": {"X": 0.5, "Y": 0.5},
          "Offsets": {"Left": 0, "Top": 0, "Right": 200, "Bottom": 40}
        }
      }
    ]
  }
}
```

## 故障排除

### API 错误
- 检查 API Key 是否正确
- 检查网络连接或代理设置
- 查看 Output Log 中的详细错误信息

### 生成失败
- 确保图片清晰，UI 元素可识别
- 尝试使用更高分辨率的截图
- 检查 JSON 格式是否正确

### 性能问题
- 大图片会增加 API 调用时间
- 建议使用 1920x1080 或更小的截图
