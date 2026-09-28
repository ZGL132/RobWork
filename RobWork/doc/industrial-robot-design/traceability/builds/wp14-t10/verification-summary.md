# WP-14-T10 验证留痕汇总（实施段）

- 任务：WP-14-T10 需求迁移（项目树/属性检查器/选择联动三接入面）——方案 B.1 迁移链第二棒。
- 分支 wp14-t10；base 60befce8a63b758a7d5daf952e03082726d7d5af；执行模型＝PIPE §0.4 单会话顺序执行（禁止子智能体）。

## 契约 verify 命令逐条

| # | 命令 | 结果 | 留痕 |
| --- | --- | --- | --- |
| 1 | `cmake --build build --config Release --target sdurws_ird_requirements` | PASS（零错误） | build-integration-full.log（全量含此目标） |
| 2 | `ctest --test-dir build/RobWorkStudio/src/rwslibs/industrialrobot/requirements -C Release -L "^ird$"` | PASS（2/2：unit＋contract） | gtest-requirements-test.xml／gtest-requirements-contract-test.xml |
| 3 | `cmake --build build --config Release --target ird_gates` | 引擎退出 1＝命中集 82 条含登记册外新增——恰增 1 条（requirements_app→requirements_plugin R1 自边，modeling_app 同型先例），登记册回填归 WP-01-T03 治理面 | ird_gates-head.log／ird_gates_head_hits_normalized.txt／ird_gates_base_head_comparison.md |
| 4 | `pwsh -File RobWork/scripts/industrialrobot/validate-docs.ps1` | PASS（20 units, 12 trace entries, 202 task files；含本任务 units/requirements.md v0.11 登记后复跑） | 本行（命令输出即证据） |

## 构建与测试

- 集成模式全量：`cmake --build build --config Release` EXIT=0 零错误（含 sdurws_ird_requirements/plugin/test/app 与 sdurws_ird_studio 宿主装配闭包）。
- 冒烟模式独立配置：`cmake -S industrialrobot -B <tmp> -DCMAKE_TOOLCHAIN_FILE=<绝对路径 vcpkg.cmake> -DCMAKE_PREFIX_PATH=D:/software/Qt/6.11.1/msvc2022_64` EXIT=0 ＋全树构建 EXIT=0 零编译错误（smoke-configure.log/smoke-build.log；Qt 前缀为 F-007/F-254 在册环境前置缺口，WP-13-T03 先例同款补充参数）。
- 单元测试：sdurws_ird_requirements_test **199 用例 0 失败**（1 例 V-22 GUI 登记跳过——GuiRegistration.V22RegisteredNotExecuted_ACC1，WP-14-T08 既定口径），其中本任务新增 HostMigration 16 用例全过。
- 契约测试：sdurws_ird_requirements_contract_test **11/11 过**（BuildGraphContractTest 白名单已随 _app 目标落位翻转——modeling T15 同款断言翻转先例）。
- GUI 冒烟：sdurws_ird_requirements_app --auto，SMOKE_EXIT=0，全链演示（deprecated-label=present／tree-nodes=4／L2 highlight TP-P1／inspector-kind=4(ObjectFields) fields=5／d6-activate=ok／l3-selected=1／L2 反例草稿不高亮／DONE）——console-requirements-app-migration-smoke.log。

## 已知边界（如实登记）

- ird_gates 登记册滞后 1 条（见上表 #3）——同 WP-14-T08 插件链接面两命中先例，回填归治理面；验收段按 detached worktree 冷启复现 base↔head 归一化裁决。
