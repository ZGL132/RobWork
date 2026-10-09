# audit-p1c-fixes 批次（P1·崩溃/UB/契约击穿）验证证据（F-594~F-602，2026-10-09）

## 修复内容（9 项 fixed，提交于隔离工作树分支 audit-p1c-fixes）

| 编号 | 单元 | 摘要 | 提交 |
| --- | --- | --- | --- |
| F-594 | testkit | SetCheck 歧义探测双方向失真→M-交错圈判定重写 | 4b858b4e |
| F-595 | testkit | 空串参数发射 ""（原实现丢参数/后续前移） | 4b858b4e |
| F-596 | testkit | 引号转义 2N+1 规则（原 N+1 参数碎裂）；函数上移 detail 命名空间 | 4b858b4e |
| F-597 | testkit | checkEnvelopeCombination 补表 3 第四规则（假绿修复） | 4b858b4e |
| F-598 | io | AtomicFile commit 失败就地删暂存（原永久泄漏） | e4803ba5 |
| F-599 | io | 固化缓存 emplace 语义（原并发插入释放已暴露缓冲＝UAF） | e4803ba5 |
| F-600 | project | redo 可用性基准改 lastServiceTip（多级撤销→重做全链恢复） | c554b6c0 |
| F-601 | project | ObjectStore cacheInsert 幂等化（同键双节点→erase(end()) UB） | 3b5654a5 |
| F-602 | diagnostics | 转译兜底路径清空调用方参数（兜底码零参数 schema 不再抛） | e27edb91 |

## 验证方式与结果

1. **新增回归测试 9 个**；三处做了撤修复红灯验证：
   - F-594 双向红灯：撤修复后"唯一匹配"用例实得 ambiguousMatch=true
     （旧误报方向实证）；且实现期发现原审查建议的"屏蔽 ri"浅修会把探
     测修死（2×2 全同值交换形态真歧义漏报）——改为 M-交错圈判定后唯一
     false／真歧义 true 双断言同过，双向钉防回退；
   - F-580/F-581/F-582/F-583/F-585（批次 B，同树已验）不赘；
   - F-595/F-596：断言精确钉住 2N+1 发射文本（旧 N+1 逻辑必不满足）；
     测试与函数上移 detail 命名空间编译耦合，独立 stash 红灯不可行，
     已在提交信息如实注明。
2. **受影响测试目标全量**：testkit 101/101、io 122/122、project 254/254
   （1 项既有登记跳过）、diagnostics 115/115。
3. **全量门禁**：`audit-p1c-fixes-gate.log`——81/85。4 项失败＝同一对
   GUI 用例（PreviewPane_MultiTypeChain_UI_T59、BatchPasteImpactDetail_
   UI_T08_ACC1）双模式各计一次，失败现场为 Qt OpenClipboard COM 重试
   耗尽（"Unable to obtain clipboard"）——**剪贴板资源竞争**：并行
   ui-t77 会话正在共享主树运行 GUI 冒烟（其产物 tour-5-final-state.png
   同期在变）。本批改动零触碰 GUI/剪贴板代码；同树批次 B 门禁（run2，
   85/85）此两用例通过——剪贴板空闲时即绿，环境性失败，非本批缺陷。
   重试一次仍失败（竞争持续），如实登记。
4. 附：隔离树环境前置已在批次 B 补齐（vcpkg junction＋python313.dll
   运行时副本），本批门禁直接复用。

## 结论

批次 C 9 项发现闭环（9 修）。分支 audit-p1c-fixes（基于 audit-p1b-
fixes@8afacad4）待验收合入。累计：F-570~F-602 共 33 项登记（26 fixed、
6 open 待裁决、1 closed 改判）。
