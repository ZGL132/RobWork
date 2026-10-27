# REUSE-T49 — WP-19 批次：testkit 单元审计收口
起始 SHA 见任务契约 note（批次串行上一收口提交）。状态：实施完成待独验。
## 判定摘要（2026-10-25 审计批）
JsonLite 21 行（上游无 JSON 库；Boost.JSON 属新依赖渠道需所有者批准，未批保留）、Check 7 行、ProcessRunner 11 行（Win32 薄封装）、Dataset 24 行、测试骨架/构建宏——全部 business-specific 收口。
## F-959 落地批（2026-10-26，所有者批量批准落地裁决表第 1~4 项，原 blocked 解阻）
- **修复面＝黄金生成脚本与 manifest 完整性登记两件**：`testdata/golden/mdl-urdf-import/1.0.0/generate/make_import_expectations.mjs`（头注计数约定 8→3＋F01 速度限位映射锚说明；`counts.unsupported` 8→3、去 joint-velocity-limit×5、增 `mappedVelocityCount=5`＋说明——与入库 expected 逐字段一致）、同目录 `manifest.json`（脚本 integrity sha256/sizeBytes 重登记 f30a5573…/7491＋history 2026-10-26 条目）。**expected 数据零变化**——按 T01 审计口径同步过期脚本，不反向重产旧错误黄金数据洗绿。
- **重放一致性（29 生成器全量收口）**：`replay-generators-t49.mjs`（Node v24.14.1，与 T01 记录同款）隔离复制 29 数据集逐脚本执行——29/29 exit0（passed=29，failed/skipped/notRun=0），规范化对照漂移 0；mdl-urdf-import 两份 expected 原始字节逐位一致（rawEqual=true）、manifest 未被生成器改写。原件 `landing-generator-replay.json`（reviewedCommit=f59b0b25e2e6…）。
- **受影响消费者真实回归**：testkit 全量套件 `sdurws_ird_testkit_test` 121/121（含 Dataset 完整性校验面——manifest integrity 全量逐文件 sha256 在装载期复核）、`sdurws_ird_modeling_test` 480/480（`MdlGoldenImport.GoldenUrdfReportCounts_V06` 消费 mappedVelocityCount 并钉 F01 映射面）、`sdurws_ird_modeling_contract_test` 17/17——三套件 PASSED 全绿，gtest XML＋per-exe ird-test-report.json 原件 landing-green-\*；ird_gates 零命中（R-1…SA02 十一类，landing-ird-gates.log）。
- **矩阵收口（全文件解决批）**：`foundation-governance.make_import_expectations.fixtureGeneration` 转 business-specific/migrated（区段随修复重铺——`const importReport` 起至文件尾、inputs 的 manifest.json 摘要级联重钉）；新增 `foundation-governance.make_import_expectations.scriptHeader` 头注/导入区段行覆盖载体（1—45 行，先例形态）；changes/REUSE-T49.json 登记 .mjs 凭证。
- **范围边界**：产品改动止于上列两件（.mjs＋manifest.json 数据面）；零 C++ 产品代码变化。`GoldenImportTest.cpp:294-296` 文档注中"忽略 2／不支持 8"字样为 T16 创建时既有过期描述（断言面数据驱动、自 T25 起即与 expected 计数一致），不属 F-959 漂移链，本批不改（改动将触发 modeling 全文件铺面扩面）——留作独立 cosmetic 缺陷由所有者归类。
- Boost.JSON 渠道批准仍为所有者保留裁决项（本批不涉）。F-959 随本批改 fixed；实施完成待独验，独立验收由所有者另开会话执行。
