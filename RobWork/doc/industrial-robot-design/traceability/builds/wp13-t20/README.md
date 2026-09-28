# WP-13-T20 验证留痕（traceability/builds/wp13-t20/）

任务：WP-13-T20 建模迁移（方案 B.1 迁移链，SA-18 D11）——TreeNodesProvider／
PropertyPagesProvider／SelectionAdapter 三接入面＋本域自持导航 deprecated 标记。

- 分支 `wp13-t20`；base＝9b36ecc0（redesign-main 尖端，UI-T22 收尾后）；送验 SHA 见验收请求。

## 文件清单

| 文件 | 内容 |
| --- | --- |
| `build-integration-full.log` | 集成模式全量构建（仓库根 `build/`，`RWS_BUILD_INDUSTRIALROBOT:BOOL=ON`）——退出码 0、零 error、115 个目标产出（增量复跑留痕） |
| `build-smoke-configure.log` / `build-smoke-build.log` | 独立冒烟模式（`-S industrialrobot -B build_smoke_t20`＋vcpkg toolchain）configure 与构建日志；`SMOKE_EXIT=0` 行为成功标记 |
| `ctest-modeling-ird.log` | 契约 verify 命令：`ctest --test-dir build/.../modeling -C Release -L "^ird$"`——2/2 通过（modeling_test＋modeling_contract_test） |
| `sdurws_ird_modeling_test.xml` | gtest XML：**282 用例 0 失败**（含 HostMigration* 十例） |
| `sdurws_ird_modeling_contract_test.xml` | gtest XML：17 用例 0 失败 |
| `ird-test-report.json` | testkit TestRecordListener 用例级明细（进程 CWD＝本目录产出） |
| `ctest-modeling-test-rerun.log` | modeling_test 直跑 stdout（offscreen） |
| `wp13-t20-gates-head.log` / `wp13-t20-gates-base.log` | ird_gates 引擎直跑命中集（head＝本分支工作树；base＝9b36ecc0 临时 worktree `../wt-t20-base`，比对后已删除） |
| `wp13-t20-gates-raw.log` | 门禁目标（构建期自定义命令）原始输出 |
| `modeling-app-migration-smoke-01.png` | GUI 冒烟①：harness 启动态——左 Dock 共享工业项目树（五分组封闭清单＋建模对象组 j1~j6/base/l1~l6）、中央五区面板（自持导航 deprecated 标记可见）、右 Dock 共享检查器（未选中对象占位）、工具条 L3 演示按钮与诚实边界状态行 |
| `modeling-app-migration-smoke-02-l3-j2.png` | GUI 冒烟②（交互）：点击"模拟 TreeView 选中 J2（L3 反解）"后——状态行"三维高亮：J2"（L2 演示出口回显）＋共享树 j2 选中高亮（L3 定位）＋自持面板关节 2 高亮（适配器联动）＋**右 Dock 检查器呈现"j2·关节常用参数"三字段（零位偏置/限位下限/限位上限，rad）＋复杂编辑"DH 参数"入口**（D5/D6 分野呈现） |
| `console-modeling-app-migration-smoke.log` | harness stdout/stderr（QFormLayout::takeRow 警告＝T15 既有呈现路径启动期一次，wp13-t15-app 留痕同源，功能无碍） |

## 门禁结论（ird_gates）

引擎直跑（`cmake -DIRD_ROOT=<...> -P cmake/ird_gates.cmake`）：**base 88 条 ↔ head 88 条，归一化
diff 仅 worktree 路径前缀差异，语义零新增命中**——共享 UI 装配面零改动（PIPE §6.2），新增文件均在
`modeling/plugin/` 既有插件白名单形态内，无需登记件。

## 诚实边界

- GUI 冒烟为开发期 harness（`sdurws_ird_modeling_app`）形态：名称映射（J1../L1..）与三维高亮
  （状态行回显）为演示绑定——名称解析权威归 runtime RuntimeNameMap（R-4），真实三维呈现归宿主
  RWStudioView3D；产品宿主挂位归 UI-T23/WP-24-T08（O-43），本演示不触 Dock 拓扑产品装配。
- 命令按钮呈现 titleKey 原文（harness 未接 ui::resolveText 文案绑定——WP-13-T15 起既有行为）。
- 编辑流经检查器常用字段页的提交链路（Outlet→域裁决）以模型层测试实证
  （HostMigrationPropertyPages.OutletTranslatesEditsToDomainFlow）；GUI 交互点验步骤登记于
  harness 文件头"用法"段。
