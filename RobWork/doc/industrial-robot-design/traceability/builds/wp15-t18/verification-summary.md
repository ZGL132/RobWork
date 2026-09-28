# WP-15-T18 验证汇总（实施段留痕）

任务：WP-15-T18 运动学迁移（方案 B.1 迁移链第三棒；DTB §4.2 O-44 接入面
v1 诚实边界——出路②契约树面收窄，契约 v1.2）。分支 wp15-t18；base
3f7dd0f11d6908099acb8d94f7b581d1a0a396fe。

## 结论一览（2026-09-29，tick#414 实施段）

| 项 | 结论 | 证据 |
| --- | --- | --- |
| 集成模式构建 | **零错误**（kinematics/plugin/test/app/contract_test 五目标；exit=0） | build-integration.log |
| 独立冒烟模式构建 | **零错误**（全树；configure exit=0＋build exit=0） | smoke-configure.log＋smoke-build.log |
| 单元测试（集成树） | **181 用例：180 过＋0 失败＋1 envUnavailable**（GUI 呈现用例按惯例如实登记不执行——GUI 手动验证归 harness 留痕，AGENTS §4.2 口径；新增 HostMigration 16 用例全过） | gtest-kinematics-test.xml＋ird-test-report-unit.json |
| 契约测试（集成树） | **57/57 过** | gtest-kinematics-contract-test.xml＋ird-test-report-contract.json |
| 冒烟树测试 | **2/2 过**（ctest -L "^ird$"） | smoke-build.log（ctest 段） |
| GUI 冒烟 | **四条 [migration-smoke] 序列全部输出**（树重建 nodeCount=0＝O-44 诚实空供给；domainKey=kinematics 注册落位；下行联动任务点选中→面板高亮；Jog 承接 applyHostJointState ok=1 零修订） | gui-migration-smoke.log |
| ird_gates | 引擎直跑 **base(3f7dd0f1)↔head 归一化零增量**（28 类＝28 类；引擎带既有登记态命中退出 1 属既有形态——wp14-t10 先例同口径；零新目标零新链接） | ird_gates-{base,head}.log＋{base,head}_hits_normalized.txt＋ird_gates_base_head_comparison.md |
| validate-task | PASS（WP-15-T18.json v1.2） | 治理提交 3f7dd0f1 批次复核 |
| validate-docs | PASS（20 units, 202 tasks） | 本目录留痕批次 |

## 交付物清单（代码面）

- 新增：`kinematics/plugin/KinHostMigrationProviders.hpp/.cpp`（三接入面
  ——TreeNodes 恒空集供给／PropertyPages 无应答面／SelectionAdapter 下行
  全功能＋上行诚实不上报）；`kinematics/test/HostMigrationTest.cpp`（16 用例）。
- 增改：KinematicsUiModule（SharedSurfaceHandles/sharedSurfaceProviders/
  attach·detachSelectionService/applyHostJointState——D8/D9 会话姿态桥域
  侧半区）；KinematicsPluginAssembly（门面四转发——UI-T23 消费面）；
  KinematicsPanelWidget（focusTaskPoint＋自持导航迁移状态标记
  kinematicsNavDeprecationLabel＋任务表 pointOid 行锚）；HarnessMain
  （共享树/检查器/选择服务迁移演示装配＋冒烟序列）；CMakeLists（源增列）。
- 文档：units/kinematics.md v0.15（§14.6 落位登记）。

## 如实登记（未执行/边界项）

- GUI 截图留痕：本会话以 harness 控制台冒烟序列留痕替代截图（GUI 呈现
  人工点验归验收段/所有者复演——DTB §5.2 DoD 第 5 条"无法执行须如实
  登记"口径：窗口启动与四条序列已程序化实证，像素级截图未采集）。
- D8/D9 的宿主 State 桥/路径发布缝：当前构建面缺位（UI-T20 交付为呈现
  发布桥），本任务交付域侧承接缝（applyHostJointState）与零修订断言，
  桥接线归宿主装配收口（UI-T23/WP-24-T08）——O-44 裁决第④项登记口径。
- 本域树面对象：v1 恒空集（O-44 诚实边界）——共享树不出现 kinematics
  节点是诚实形态非缺陷；前瞻填充等所有者对象身份模型裁决（B1-SPEC §3.1
  行 4/5 前瞻形态）。
