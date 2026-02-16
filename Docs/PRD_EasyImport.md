# PRD: Design2UMG 傻瓜式导入方案

## 1. 概述

为 Design2UMG 插件新增**双路径导入**能力，让用户（TA、UI 设计师、策划）无需命令行知识，在 UE5 编辑器里点几下就能完成从 Pencil 设计稿到 Widget Blueprint 的全部流程。

| 路径 | 名称 | 优先级 | 依赖 |
|------|------|--------|------|
| A | 离线 .pen 文件导入 | P0 | 无 |
| B | 在线 Pencil 实时同步 | P1 | 运行中的 Pencil |

## 2. 背景

### 2.1 现有管线

```
.pen JSON → PenParser → Design2UMGConverter → LayoutFixer → LayoutCalculator → UMGGenerator → Widget Blueprint
```

### 2.2 现有入口

| 入口 | 接口 | 问题 |
|------|------|------|
| 控制台命令 | `Design2UMG.PenConvert <nodes.json> <vars.json>` | 需要手动拆分 nodes/vars，非傻瓜式 |
| C++ API | `FPenImporter::ImportFromFile(FilePath)` | 无 UI 入口 |
| C++ API | `FPenImporter::ImportFromJson(JsonString)` | 无 UI 入口 |

### 2.3 调研结论（已验证）

1. **.pen 文件是纯明文 JSON**，与 PenParser 期望格式完全一致，`ParseFromFile` 可直接解析
2. **Pencil MCP Server 支持 HTTP 模式**：`mcp-server-windows-x64.exe -app antigravity -http -http-port 8080`
3. **MCP HTTP 协议**：JSON-RPC 2.0 + `Mcp-Session-Id` 头，端点 `http://localhost:PORT/mcp`
4. **MCP Server 需要运行中的 Pencil 实例**（`-app antigravity` 连接 VS Code fork）

### 2.4 目标

一句话：**设计师在 Pencil 里画完，在 UE5 里一键变成 Widget Blueprint。**

## 3. 路径 A：离线 .pen 文件导入（P0）

### 3.1 用户故事

1. 设计师在 Pencil 中完成 HUD 设计，Ctrl+S 保存为 `.pen` 文件
2. 在 UE5 编辑器中，通过以下任一方式导入：
   - **工具栏按钮**：点击 "Import .pen" → 文件选择对话框 → 选择 .pen 文件
   - **Content Browser 右键**：Import → Pencil Design (.pen)
   - **拖拽**：直接拖 .pen 文件到 Content Browser
3. 弹出预览窗口，确认后生成 Widget Blueprint

### 3.2 入口点

#### 3.2.1 工具栏按钮

在 UE5 编辑器主工具栏添加 "Design2UMG" 下拉菜单：

| 菜单项 | 功能 |
|--------|------|
| Import .pen File... | 打开文件对话框，选择 .pen 文件导入 |
| Connect to Pencil... | 路径 B 入口（见第 4 节） |
| Settings | 打开插件设置面板 |

#### 3.2.2 Content Browser 集成

注册 `UFactory` 子类，支持：
- Content Browser 右键 → Import Asset → 识别 `.pen` 扩展名
- 拖拽 `.pen` 文件到 Content Browser 触发导入

#### 3.2.3 控制台命令（保留并增强）

```
Design2UMG.Import <file.pen> [--out=/Game/Path] [--no-preview]
```

简化现有 `PenConvert` 命令，直接接受 .pen 文件路径。

### 3.3 数据流

**单 frame（快速路径）**：
```
用户选择 .pen 文件
    ↓
FPenImporter::ImportFromFile(FilePath, OutData)
    ↓
FPenRemoteImageImporter::ResolveRemoteImages
    ↓
TryOpenPreviewWindow → FUMGGenerator::GenerateWidgetBlueprint
```

**多 frame（选择路径）**：
```
用户选择 .pen 文件
    ↓
FPenParser::ParseFromFile → FPenDocument  （仅解析，不转换）
    ↓
提取顶层 frame 列表 → Frame 选择器对话框
    ↓
用户选择 TargetNodeId
    ↓
FPenMCPBridge::ConvertFromMCP(FileJson, "{}", OutData, TargetNodeId)
    ↓  （复用 MCP Bridge 的 TargetNodeId 过滤能力）
FPenRemoteImageImporter::ResolveRemoteImages
    ↓
TryOpenPreviewWindow → FUMGGenerator::GenerateWidgetBlueprint
```

**关键设计决策**：多 frame 时不走 `ImportFromFile`（它会转换所有顶层 children），而是先解析 JSON 再走 `ConvertFromMCP` 路径，复用其已有的 `TargetNodeId` 过滤能力（`PenMCPBridge.cpp:64`）。这样不需要修改 `PenImporter` 或 `Design2UMGConverter`。

### 3.4 Frame 选择器

一个 .pen 文件可能包含多个顶层 frame（如设计系统组件库 + 多个页面）。导入时需要让用户选择要导入哪个 frame。

**交互流程**：
1. 用 `FPenParser::ParseFromFile` 解析 .pen 文件，遍历 `Document.Children` 提取所有顶层 frame 的 `id`、`name`、`width`、`height`
2. 如果只有 1 个顶层 frame，直接走快速路径（`ImportFromFile`）
3. 如果有多个，弹出选择对话框（`SListView` + `SWindow` 内联）
4. 用户选择后，读取 .pen 文件原始 JSON，以选中 frame 的 `id` 作为 `TargetNodeId`，走 `ConvertFromMCP` 路径

## 4. 路径 B：在线 Pencil 实时同步（P1）

### 4.1 用户故事

1. 设计师在 Pencil 中正在编辑 HUD 设计
2. 在 UE5 编辑器中，点击工具栏 "Connect to Pencil..."
3. 插件自动发现本地 Pencil 实例，建立连接
4. 弹出 Frame 选择器，列出 Pencil 中所有顶层 frame
5. 选择后，获取设计数据 → 预览 → 生成 Widget Blueprint

### 4.2 连接流程

```
用户点击 "Connect to Pencil..."
    ↓
FPencilConnector::Discover()  — 查找 MCP Server 二进制
    ↓
FPencilConnector::Launch()    — 启动 mcp-server -app antigravity -http -http-port PORT
    ↓
FMCPHttpClient::Connect(Endpoint)  — 内部完成 initialize 握手 + notifications/initialized
    ↓
FMCPHttpClient::CallTool("get_editor_state") — 获取当前编辑器状态
    ↓
FMCPHttpClient::CallTool("batch_get", readDepth=1) — 获取顶层 frame 列表
    ↓
Frame 选择器对话框
    ↓
FMCPHttpClient::CallTool("batch_get", nodeIds=[选中frame], readDepth=99) — 获取完整节点树
FMCPHttpClient::CallTool("get_variables") — 获取变量定义
    ↓
FPenMCPBridge::ConvertFromMCP(NodesJson, VarsJson, TargetNodeId)
    ↓
预览 → 生成 Widget Blueprint
```

### 4.3 MCP Server 发现与启动

#### 4.3.1 发现逻辑

按优先级搜索 MCP Server 二进制：

1. 插件设置中用户手动指定的路径
2. `%USERPROFILE%/.antigravity/extensions/highagency.pencildev-*/out/mcp-server-windows-x64.exe`
3. `%USERPROFILE%/.vscode/extensions/highagency.pencildev-*/out/mcp-server-windows-x64.exe`

跨平台二进制名：

| 平台 | 文件名 |
|------|--------|
| Windows | `mcp-server-windows-x64.exe` |
| macOS ARM | `mcp-server-darwin-arm64` |
| macOS Intel | `mcp-server-darwin-x64` |
| Linux | `mcp-server-linux-x64` |

#### 4.3.2 App Name 发现

按优先级确定 `-app` 参数：

1. 插件设置中用户手动指定
2. 检测 `antigravity` 进程是否运行 → `"antigravity"`
3. 检测 `code` 进程是否运行 → `"code"`（标准 VS Code）
4. 默认 `"antigravity"`

### 4.4 MCP HTTP 客户端接口

```cpp
/** MCP Streamable HTTP 客户端 */
class FMCPHttpClient
{
public:
    /**
     * 连接到 MCP Server 并完成握手。
     * 内部执行：
     *   1. POST initialize（Accept: text/event-stream）→ 从响应头提取 Mcp-Session-Id
     *   2. POST notifications/initialized（携带 Mcp-Session-Id）
     * 成功后 IsConnected() 返回 true。
     */
    bool Connect(const FString& Endpoint);

    /** 断开连接 */
    void Disconnect();

    /**
     * 调用 MCP 工具（同步，阻塞 Game Thread）。
     * OutResult 为 result.content[0].text 解包后的纯 JSON 字符串，
     * 可直接传给 FPenMCPBridge::ConvertFromMCP。
     *
     * 注意：响应格式为 SSE（text/event-stream），
     * 客户端需提取 "data:" 行后的 JSON-RPC body。
     */
    bool CallTool(const FString& ToolName,
                  const TSharedPtr<FJsonObject>& Arguments,
                  FString& OutResult);

    /**
     * 调用 MCP 工具（异步，不阻塞 Game Thread）。
     * 回调在 Game Thread 触发。Result 格式同 CallTool。
     * 注意：不支持并发调用，必须等上一次完成后再发起下一次。
     */
    void CallToolAsync(const FString& ToolName,
                       const TSharedPtr<FJsonObject>& Arguments,
                       TFunction<void(bool bSuccess, const FString& Result)> OnComplete);

    bool IsConnected() const;

private:
    FString ServerEndpoint;   // http://localhost:PORT/mcp
    FString SessionId;        // Mcp-Session-Id
    int32 NextRequestId = 1;
};
```

### 4.5 Pencil 连接器

```cpp
/** Pencil 实例发现与 MCP Server 生命周期管理 */
class FPencilConnector
{
public:
    /** 发现本地 MCP Server 二进制路径 */
    static FString DiscoverMCPServerPath();

    /** 检测 Pencil 应用名称 */
    static FString DetectAppName();

    /** 启动 MCP Server 并返回 HTTP 端点 */
    bool Launch(int32 Port = 0);

    /** 关闭 MCP Server 子进程 */
    void Shutdown();

    /** 获取 MCP HTTP 客户端 */
    FMCPHttpClient& GetClient();

    bool IsRunning() const;

private:
    FProcHandle ServerProcess;
    TUniquePtr<FMCPHttpClient> Client;
    int32 ServerPort = 0;
};
```

Port=0 时自动选择可用端口（从 18900 开始递增探测）。

### 4.6 MCP HTTP 协议细节

已验证的协议流程：

| 步骤 | 方法 | 请求头 | 说明 |
|------|------|--------|------|
| 1 | POST `/mcp` | `Accept: text/event-stream` | `initialize` 握手，响应头返回 `Mcp-Session-Id` |
| 2 | POST `/mcp` | `Mcp-Session-Id: xxx` | `notifications/initialized` 通知（无响应体） |
| 3+ | POST `/mcp` | `Mcp-Session-Id: xxx`, `Accept: text/event-stream` | `tools/call` 调用具体工具 |

`tools/call` 请求格式：
```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "batch_get",
    "arguments": { "readDepth": 99 }
  },
  "id": 3
}
```

**响应解析**：

响应格式为 SSE（`Content-Type: text/event-stream`），包含一个或多个事件帧：
```
event: message
data: {"jsonrpc":"2.0","result":{"content":[{"type":"text","text":"..."}]},"id":3}
```

客户端解析步骤：
1. 提取 `data:` 行后的 JSON 字符串
2. 解析 JSON-RPC body，取 `result.content[0].text`
3. 该 `text` 即为工具返回的纯 JSON，可直接传给 `FPenMCPBridge`

## 5. 技术约束

### 5.1 依赖

- 仅依赖 Design2UMG 插件现有模块 + UE 核心模块
- MCP HTTP 客户端使用 UE 内置 `FHttpModule`
- 子进程管理使用 `FPlatformProcess::CreateProc`
- 不引入外部库

### 5.2 不修改的代码

- PenParser（已验证兼容）
- Design2UMGConverter
- LayoutFixer / LayoutCalculator
- UMGGenerator
- SDesign2UMGPreviewWindow（复用现有预览窗口）

### 5.3 最小修改原则

遵循 Linus 法则——**改动越少越好**。

**新增文件（仅必要的）**：

| 文件 | 职责 |
|------|------|
| `PenFileImportFactory.h/cpp` | UFactory 子类，处理 .pen 拖拽/右键导入 |
| `MCPHttpClient.h/cpp` | MCP Streamable HTTP 客户端 |
| `PencilConnector.h/cpp` | MCP Server 发现、启动、生命周期 |

**修改文件（最小改动）**：

| 文件 | 改动 |
|------|------|
| `Design2UMGEditorModule.cpp` | `StartupModule` 中注册工具栏按钮和新控制台命令 |
| `Design2UMGEditor.Build.cs` | 新增 `ToolMenus` 依赖（工具栏菜单注册所需） |
| `Design2UMGSettings.h/cpp` | 新增 MCP Server 路径、App Name 两个可选设置字段 |

**不新增的文件**：
- 不新增 Settings UI（复用 `UDeveloperSettings` 已有机制）
- 不新增 Frame 选择器 Widget（用 `SListView` + `SWindow` 内联实现）

## 6. 验收标准

### 6.1 路径 A

| # | 场景 | 预期 |
|---|------|------|
| A1 | 拖拽单 frame 的 .pen 文件到 Content Browser | 弹出预览窗口，确认后生成 Widget Blueprint |
| A2 | 拖拽多 frame 的 .pen 文件 | 弹出 Frame 选择器 → 选择后预览 → 生成 |
| A3 | 工具栏 Import .pen File → 选择文件 | 同 A1/A2 |
| A4 | 控制台 `Design2UMG.Import test.pen` | 同 A1 |
| A5 | 选择非 .pen 文件 | 提示格式错误，不崩溃 |

### 6.2 路径 B

| # | 场景 | 预期 |
|---|------|------|
| B1 | Pencil 运行中，点击 Connect to Pencil | 自动发现并连接，显示 frame 列表 |
| B2 | 选择 frame 后确认 | 获取数据 → 预览 → 生成 Widget Blueprint |
| B3 | Pencil 未运行 | 提示 "未检测到 Pencil，请先启动 Pencil" |
| B4 | MCP Server 二进制不存在 | 提示 "未找到 Pencil MCP Server，请安装 Pencil 扩展" |
| B5 | 连接中断 | 提示错误，不崩溃，可重试 |

## 7. 实现顺序

```
Phase 1: 路径 A 核心（P0）
  ├─ PenFileImportFactory（拖拽 + 右键导入）
  ├─ 工具栏按钮 + Import .pen 命令
  └─ Frame 选择器对话框

Phase 2: 路径 B 核心（P1）
  ├─ MCPHttpClient
  ├─ PencilConnector（发现 + 启动）
  └─ Connect to Pencil 工具栏入口
```
