# WP-15-T02 IK 候选

- 需求/阶段：KIN-02、03；B/R1
- 契约：`architecture/domain-model.md`、`architecture/testing-contract.md`
- 前置：WP-15-T01、WP-02；允许：IK 核心和测试；禁止：修改候选应用命令
- 产出：残差、限制、去重、稳定 ID 和排序键
- Given 超出限制或残差，When 求 IK，Then 候选不可行且原因可见
- Given 重复解，When 排序，Then 去重并按应用状态/残差/稳定 ID 稳定排序
- 命令：`run-tests.ps1 ... -Regex '^sdurws_ird_kinematics_test$'`
- 证据：IK 黄金数据和排序报告；提交：`WP-15-T02: implement ik candidates`
- 停止：候选排序键未冻结时暂停
