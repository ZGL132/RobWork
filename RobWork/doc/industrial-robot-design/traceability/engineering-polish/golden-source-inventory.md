# 既有黄金数据来源盘点

29 个数据集的 manifest 声明摘录；118 文件 hash/size 已实算全匹配。independentOfProductionImpl 是原数据声明，不等于本轮逐行审核其生成器；本轮另写标准库独立参考核对 FK/合成物性。fixture 只能证明契约场景，不能自动充当物理解析真值。既有仓内数据沿用仓库许可，无 Isaac/厂商资产导入。

| 数据集 | 种类 | 参考法 | 声明独立 | 生成器 |
| --- | --- | --- | --- | --- |
| dt-mapping-golden | analytic-case | closed-form | True | generate/make_dt_mapping_golden.mjs |
| dyn-two-link-analytic | analytic-case | closed-form | True | generate/make_dyn_two_link_golden.mjs |
| ev-slice-fixture | contract-fixture | 未声明 | 未声明 | generate/make_ev_slice_fixture.mjs |
| kin-coverage | contract-fixture | 未声明 | 未声明 | generate/make_kin_coverage_golden.mjs |
| kin-fk-analytic | analytic-case | closed-form | True | generate/make_kin_fk_golden.mjs |
| kin-ik-dedup | contract-fixture | 未声明 | 未声明 | generate/make_kin_ik_dedup_golden.mjs |
| kin-search-outcomes | contract-fixture | 未声明 | 未声明 | generate/make_kin_search_outcomes_golden.mjs |
| mdl-dh-equivalence | analytic-case | closed-form | True | generate/make_dh_equivalence.mjs |
| mdl-not-expressible | contract-fixture | 未声明 | 未声明 | generate/make_not_expressible.mjs |
| mdl-template-6r | contract-fixture | 未声明 | 未声明 | generate/make_template_golden.mjs |
| mdl-urdf-import | contract-fixture | 未声明 | 未声明 | generate/make_import_expectations.mjs |
| opt-adopt-golden | contract-fixture | closed-form | True | generate/make_opt_adopt_golden.mjs |
| opt-lhs-golden | analytic-case | independent-program | True | generate/make_opt_lhs_golden.mjs |
| opt-pareto-golden | analytic-case | independent-program | True | generate/make_opt_pareto_golden.mjs |
| pol-collision-analytic | analytic-case | hand-built | True | generate/make_pol_collision_analytic.mjs |
| pol-policy-codec-fixture | contract-fixture | 未声明 | 未声明 | generate/make_pol_policy_fixture.mjs |
| req-partial-pose | contract-fixture | 未声明 | 未声明 | generate/make_partial_pose_golden.mjs |
| req-payload-events | contract-fixture | 未声明 | 未声明 | generate/make_payload_events_golden.mjs |
| req-region-sampling | contract-fixture | 未声明 | 未声明 | generate/make_region_sampling_golden.mjs |
| req-transport-task | contract-fixture | 未声明 | 未声明 | generate/make_transport_task_golden.mjs |
| rt-fk-equation | analytic-case | closed-form | True | generate/make_fk_equation.mjs |
| rt-namemap-roundtrip | contract-fixture | 未声明 | 未声明 | generate/make_namemap.mjs |
| sel-backfill-golden | analytic-case | closed-form | True | generate/make_sel_backfill_golden.mjs |
| sel-catalog-golden | contract-fixture | closed-form | True | generate/make_sel_catalog_golden.mjs |
| sel-combo-golden | analytic-case | closed-form | True | generate/make_sel_combo_golden.mjs |
| sel-linear-golden | contract-fixture | closed-form | True | generate/make_sel_linear_golden.mjs |
| sel-screening-golden | analytic-case | closed-form | True | generate/make_sel_screening_golden.mjs |
| trj-at06-recheck-golden | analytic-case | closed-form | True | generate/make_trj_at06_golden.mjs |
| wf-gate-advice-fixture | contract-fixture | hand-built | True | generate/make_wf_gate_advice_fixture.mjs |
