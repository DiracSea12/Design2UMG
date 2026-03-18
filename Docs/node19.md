### #19 MCPHttpClient (1H 2M 2L)

| 级别 | 问题 | 位置 |
|------|------|------|
| H-并发 | `CallToolAsync` 与 `SendRequest` 各自递增 `NextRequestId++`，但 `CallToolAsync` 的回调在 HTTP 线程触发，若与 Game Thread 上的 `SendRequest`/`CallTool` 交叉调用，`NextRequestId` 存在数据竞争（无锁、无原子操作）。注释说"不支持并发调用"，但代码层面没有任何强制互斥，调用方一旦违反约定就是 UB | `NextRequestId`（.h:52, .cpp:76, .cpp:256） |
| M-重复 | `CallToolAsync` 手动重建了 JSON-RPC 请求构造 + HTTP 发送的全部逻辑（.cpp:252-274），与 `SendRequest`（.cpp:60-161）高度重复。协议细节（header、jsonrpc 版本、notification 判断）分散在两处，改一处漏另一处就会产生不一致 | `CallToolAsync` vs `SendRequest` |
| M-API | `CallToolAsync` 没有设置 `SetTimeout`，而 `SendRequest` 设了 30s。异步请求如果服务端无响应，HTTP 请求将无限挂起，永远不会触发回调 | `CallToolAsync`（.cpp:268-274） |
| L | `SendRequest` 的同步等待循环中调用 `FSlateApplication::Get().Tick()` 做 UI 泵送。这会导致编辑器 UI 事件重入——用户在等待期间点击按钮可能再次触发 `CallTool`，形成递归阻塞。`PumpMessages` 已足够保持窗口响应，`Tick()` 是多余且危险的 | `SendRequest` 等待循环（.cpp:154-155） |
| L | `SetNumberField("id", NextRequestId++)` 使用 `double` 存储 JSON-RPC id。当 `NextRequestId` 超过 2^53 时精度丢失（实际场景几乎不可能触发，但 JSON-RPC 规范允许字符串 id，用 `SetStringField` + `FString::FromInt` 更规范） | `SendRequest`（.cpp:76）、`CallToolAsync`（.cpp:256） |
