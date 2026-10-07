# UI-T73 构建与测试留痕（宿主 ④ 端口缺省策略源批次——O-46 裁决出路②-scope）

分支 ui-t73（基点 redesign-main@4f2ebbaf）；任务契约 tasks/foundation/UI-T73.json；
DTB §4.2 O-46 已裁决＋§2.11 WP-10-T73 行；units/ui.md v1.86 §13 UI-T73 行；units/policy.md v0.16 §3.1/§9.10。

## 交付物

- policy 新公共面 SystemDefaultPolicy（.hpp/.cpp）：保留身份＋附录 D 缺省集工厂＋
  SystemDefaultPolicyProvider 解析半区供给器；test/SystemDefaultPolicyTest.cpp 五用例。
- 宿主装配换装：UiPlugin.hpp 成员＋UiPlugin.cpp HandlerServices nullptr→缺省源；
  mtour step5 诚实中间态断言（F-546）。

## 验证记录

| 项 | 结果 | 留痕 |
| --- | --- | --- |
| 受影响目标集成构建（policy 库＋policy_test＋sdurws_ird_studio） | 零错误 | 会话执行记录 |
| policy_test | 206/206（＋SystemDefaultPolicy 五用例） | logs/policy_test.log |
| policy_contract_test | 23/23 | logs/policy_contract_test.log |
| 六套件回归 | modeling_gui 42＋requirements_gui 45＋modeling_test 339＋modeling_contract 17＋ui_test 254＋ui_contract 32 全绿 | logs/ 六件 |
| 真机 modeling-tour | MTOUR_PASS（exit=0）——step5 断言面＝④装配缺陷已消除（rej 从「④策略端口未装配」→ hard-assert-failed＝拒绝面移出行程策略层）；undo-unavailable-no-revision | smoke-tour-modeling/ |
| 真机 requirements-tour（回归） | TOUR_PASS（exit=0） | smoke-tour/ |
| ird_gates | 零命中 exit 0 | ird-gates.log |
| validate-docs／validate-task（UI-T73.json） | PASS／PASS | 会话执行记录 |

## 诚实边界

- **F-546（major，本批新登）**：④行程校验需运行时名解析，首应用（发布前）名称映射为空→CllNameUnresolved→EvaluationFailed→RejectedHardAssert——行程相关应用主干的首应用在真实宿主结构性地不可达（与策略源无关）。mtour step5 断言为诚实中间态（翻转点内建）；三路修复方向已列 F-546 owner（候选感知上下文／校验时点后移／首应用豁免）待所有者裁决。
- F-536 保持 open（进展注）：④装配缺陷面已消除，但「主链可用」口径以 F-546 修复为完成——不虚报。
- P-POL-2/O-10 口径不变：未裁决阈值 nullopt＝显式不适用，本批零数值发明；REQUIREMENTS.md 零改动。
