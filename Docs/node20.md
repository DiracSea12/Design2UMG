### #20 FigmaHttpClient (1H 2M 1L)

| 级别 | 问题 | 位置 |
|------|------|------|
| H-API | NodeIds 未对冒号做 URL 编码/替换，Figma node ID 格式为 `0:1`，直接拼入 query string 可能被代理/CDN 截断，且与 Figma 官方要求的 `0-1` 格式不一致 | `ExportImages` → `SendGet` URL 拼接 (cpp:24-25) |
| M-资源 | HTTP 请求无取消机制，异步回调捕获的外部对象（如 `TSharedRef<FDesignDocument>`）在模块卸载或对象销毁时可能已失效，导致悬垂引用 | `SendGet` lambda 捕获 (cpp:35-54)，调用方在 `FigmaImageExporter` 中捕获 Document |
| M | `FigmaApiBase` 是 file-scope static `FString`（非 trivial 全局对象），受 Static Initialization Order Fiasco 影响；改用 `constexpr TCHAR[]` 或函数内 static 更安全 | cpp:10 `static const FString FigmaApiBase` |
| L | Token 以明文 `FString` 长期驻留内存（`GFigmaClient` 生命周期 = 编辑器进程），无使用后清除机制 | `Token` 成员变量 (h:33)，`SetToken` (h:18) |
