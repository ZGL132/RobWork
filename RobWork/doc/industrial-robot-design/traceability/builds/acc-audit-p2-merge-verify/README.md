# audit-p2 合入后全量复验证据（2026-10-09）

合并序列：redesign-main 快进并 origin（21fa8d58，WP-19-T11）→ 按 SHA 合 evidence
e50687c9（39a9e386）→ 按冻结 SHA 合链尾 c0bdd020（零冲突）→ 推送前再并 origin
增量 96d6cd9d（WP-20-T11，产品面零交集）。

## 复验矩阵（合并树 10382bb9）

| 项 | 结论 | 证据 |
| --- | --- | --- |
| 集成构建 | sdurws_ird_* 零错误（宿主外插件失败属 DTB §5.1 排除面） | verify.log |
| ird_gates | PASS×2（两轮） | gates.log、gates2.log |
| validate-docs | PASS | validate-docs.log |
| 集成＋冒烟 sweep | 40/40 全绿（剪贴板预探生效，零失败零误归因） | sweep-summary.txt |
| optimization 集成面 | EXIT=0（显式重配后补建——F-623 陈旧 solution 陷阱实测） | sweep-opt2.log |
| testdata lint | 26 集 1 违规＝sel-catalog-golden 引用字典外 AT-08（WP-19-T11 交付面，F-624 登记） | lint.log |

## 治理登记（同提交 findings.json）

- F-555/F-556：所有者预裁决——ui-t77 合流时以 ui-t77 版 8597c135 为准，1a9b5523 弃用
- F-618：所有者排期——audit-p2 后独立批次（先单元卡后代码，收编 RT-BOUNDS-MAGNITUDE）
- F-623：VS 增量构建陈旧 solution 陷阱（新测试目标静默缺位，复验前须显式重配）
- F-624：sel-catalog-golden 字典失配（AT-08）
