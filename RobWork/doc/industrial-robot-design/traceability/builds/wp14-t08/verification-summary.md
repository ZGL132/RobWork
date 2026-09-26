# WP-14-T08 验证留痕总结（实施段）

- 任务：WP-14-T08（requirements 插件界面——四面板与编辑→草稿→应用链路）
- 分支：wp14-t08（自 base 17ff09969db3d4adf9a07b12491c61c22498a148 建立）
- 执行环境：worktree `D:/wt-wp14-t08`（主检出由并发 wp24-t03 会话持有，本任务
  全程未触碰主检出；base 侧门禁比对用临时 worktree `D:/wt-wp14-t08-base` 已删）
- 构建树：集成 `build/`（仓库根口径——本 worktree 内自配，
  `RWS_BUILD_INDUSTRIALROBOT:BOOL=ON` 已 grep 确认）；独立冒烟 `build-smoke/`
  （带 vcpkg toolchain＋Qt prefix——T07 先例口径）
- 执行日期：2026-09-26

## 1. 双模式构建（acceptance 6）

| 模式 | 命令 | 结果 |
| --- | --- | --- |
| 集成 | `cmake --build build --config Release --target sdurws_ird_requirements_plugin` | 退出码 0，零错误（build-integrated-plugin.log） |
| 集成 | `cmake --build build --config Release --target sdurws_ird_requirements_test sdurws_ird_requirements_contract_test` | 退出码 0，零错误（build-integrated-tests.log） |
| 冒烟 | `cmake -S …/industrialrobot -B build-smoke -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=<vcpkg> -DCMAKE_PREFIX_PATH=<Qt6.11.1>` | 退出码 0（smoke-configure.log） |
| 冒烟 | `cmake --build build-smoke --config Release --target sdurws_ird_requirements_plugin sdurws_ird_requirements_test sdurws_ird_requirements_contract_test` | 退出码 0，零错误（smoke-build.log） |

补充事实：worktree 缺 gitignored 的 `RobWorkStudio.ini.template.static`——
首次集成配置失败（configure_file 找不到文件）；从主检出**只读复制**该模板
（及 ini.shared.in）后配置成功。冒烟模式 Qt 路径为 testkit_qt（UI-T11 起）
与插件目标（本任务起）的既有解析前提，T07 冒烟缓存同款（非新增依赖）。

## 2. 测试执行与留痕（acceptance 1~5 具名自证）

| 项 | 结果 |
| --- | --- |
| `sdurws_ird_requirements_test`（含新增 PluginPanelTest 14 用例） | **161/161 通过**（gtest-requirements-test.xml；ird-test-report-unit.json summary: total 161, passed 161, failed 0, decisive true） |
| `sdurws_ird_requirements_contract_test`（含 T08 随附同步后的构建图断言） | 5/5 通过（gtest-requirements-contract-test.xml；ird-test-report-contract.json） |
| `ctest --test-dir …/requirements -C Release -L "^ird$"` | 2/2 通过（ctest.log；另连续 6 轮复跑全绿） |
| 冒烟模式同 exe 直跑 | 161/161 通过（两模式口径一致） |

across 测试稳定性修正记录：初版 PluginPanelTest 存在 4 处缺陷并逐一修复
（①夹具条目未按 I-REQ-1 排序/计划条目全零 id——随机性偶发；②树断言误用
插入序；③校验用例误传空闭包＋R4 短路后 R5 不执行的层序误设；④
ValueProvenance::make 签名误用）——均为测试自身缺陷，产品代码零改动。
最终版本连续多轮全绿。

## 3. 门禁（ird_gates 引擎直跑 base..head 归一化比对）

详见 `ird_gates_base_head_comparison.md`。结论：恰两条新增命中——
`requirements->requirements`（插件→本单元计算库自边）与 `requirements->ui`
（插件→ui），均为卡 §3.2 明文插件链接面的引擎解析结果（modeling WP-13-T15
同型），契约 sanctioned；白名单回填归 WP-01-T03 治理面。R-3 对插件目标零命中。

## 4. 治理脚本

| 脚本 | 结果 |
| --- | --- |
| `pwsh -File RobWork/scripts/industrialrobot/validate-docs.ps1` | PASS（20 units, 12 trace entries, 189 task files） |
| `pwsh -File RobWork/scripts/industrialrobot/validate-task.ps1 -TaskFile …/WP-14-T08.json` | PASS（1 tasks） |

## 5. V-22 GUI 流程（acceptance 5）

设计登记于 `v22-gui-flow-registration.md`（8 用例规程＋AGENTS Windows 规程
QT_QPA_PLATFORM=windows 逐个绝对路径启动）。**本次未启动任何 GUI 程序**
（执行机锁屏/无交互桌面——envUnavailable，如实登记，不记绿灯）；呈现层
验证随 WP-24-T03 装配任务回填。

## 6. 契约 verify 命令逐条执行记录

| 契约 verify | 执行 | 结果 |
| --- | --- | --- |
| `cmake --build build --config Release --target sdurws_ird_requirements_plugin` | ✅ 已执行（集成构建树＝本 worktree 自配） | 零错误 |
| `ctest --test-dir build/RobWorkStudio/src/rwslibs/industrialrobot/requirements -C Release -L "^ird$"` | ✅ 已执行 | 2/2 通过 |
| `pwsh -File RobWork/scripts/industrialrobot/validate-docs.ps1` | ✅ 已执行 | PASS |
