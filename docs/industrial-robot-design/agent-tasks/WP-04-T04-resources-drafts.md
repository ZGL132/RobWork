# WP-04-T04 内容对象、资源与草稿隔离

- Task ID：WP-04-T04
- 需求/阶段：CON-01、CON-03、NFR-REL-01；阶段 A / R1
- 架构契约：`architecture/persistence-schema.md`、`architecture/execution-model.md`、`architecture/testing-contract.md`；模块方案：`module-design/persistence.md`
- 前置：WP-04-T01、WP-04-T03、WP-03 core。

## 边界与产出

允许：修改 `include/sdurws/ird/project/ContentObject.hpp`、`ProjectDraft.hpp`、`src/ContentObjectStore.cpp`、`src/DraftStore.cpp`、`src/Reachability.cpp`、`test/ResourceDraftTest.cpp`。
禁止：修改 evaluator/result 接口、ProjectRevision 字段含义、GUI 会话状态或直接删除历史对象。

产出：内容寻址对象库、引用图遍历、dry-run 清理报告和独立草稿库。对象路径为 `objects/<lowercase sha256>`，写入后只读；写入相同哈希必须幂等并校验 mediaType/bytes。

## 数据流与规则

`external path -> safe read -> size/type/hash limits -> object store -> ContentObjectRef -> manifest/objectRefs`。Verified 或正式报告引用资源时必须复制；源文件变化不改写历史对象。可达根包括所有 branch HEAD、历史 revision、snapshot、report、checkpoint。草稿 `draftId/sessionId/baseRevisionId/patch/editedAt` 单独写入 drafts，绝不进入 revision 或计算队列。

## Given/When/Then

- Given 外部网格/URDF/目录/材料表，When 导入并被 Verified 引用，Then产生不可变 SHA-256 对象和 manifest 引用。
- Given 相同字节重复导入，When put，Then返回同一 objectId，不重复写入且哈希一致。
- Given 对象被任一历史 revision、报告或检查点引用，When collect，Then列为 reachable，不得删除。
- Given 未保存草稿，When session reload 或 evaluator enqueue，Then草稿恢复到会话但不产生 revision、不创建输入切片、不触发计算。
- Given 源文件缺失或超过预算，When import，Then返回 `IRD-PERSIST-SOURCE-MISSING` 或 `IRD-PERSIST-RESOURCE-BUDGET`，旧项目不变。

## 测试与命令

覆盖对象篡改、媒体类型不符、超大文件、不可达对象 dry-run、引用图环、草稿并发保存、草稿损坏和会话重启。

命令：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_resources_drafts_test$'
```

证据：对象哈希清单、可达性报告、草稿与 revision 对比、队列调用计数、诊断 JSON 和评审签名。

提交：`WP-04-T04: implement immutable resources and draft isolation`。

停止：清理算法无法证明历史可达性、草稿可能进入 evaluator 或需要改变快照契约时暂停。
