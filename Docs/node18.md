### #18 UMGJsonSerializer (1H 1M)

| 级别 | 问题 | 位置 |
|------|------|------|
| H-数据丢失 | `SerializeSlot` 未序列化 `bAutoSize` 字段。`FUMGSlotData::bAutoSize` 在 LayoutMapper、UMGGenerator、UMGSlotApplier 等多处被读写，但序列化输出完全忽略了它。Serialize→Parse 往返后 `bAutoSize=true` 的节点会静默退化为 `false`，导致预览窗口手动编辑后 Size To Content 丢失 | `SerializeSlot()` |
| M-数据丢失 | `SerializeWidgetNode` 未序列化 `TextStyle` 和 `Brush` 两个 v2.4 类型化结构体。当前这两个字段的数据仅存在于内存中的 `FUMGWidgetNode`，经 JSON 往返后全部丢失。目前影响有限（因为 Parser 端也未解析这两个结构体，说明 JSON 通路尚未启用 v2.4 类型化字段），但随着 Properties→类型化迁移推进，此处会成为阻断点 | `SerializeWidgetNode()` |
