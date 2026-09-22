# wp10-t15 门禁命中集登记（UI-T15 / WP-10-T15）

- 任务：工作台验证 harness `sdurws_ird_ui_app`（开发期 L5 装配雏形＋可视化验证入口）
- base 修订：`2b5a8335`（redesign-main 分支 HEAD，requirements 单元详设 v0.1 提交）
- 分支：`wp10-t15`
- 日期：2026-09-22
- 引擎：`industrialrobot/cmake/ird_gates.cmake`（WP-01-T01；数据面 `ird_gates_whitelist.cmake`）
- 留痕：`traceability/builds/wp10-t15/ird_gates-base.txt`（base 全量命中）、
  `ird_gates-wp10-t15.txt`（本分支全量命中）；比对口径＝命中行排序去重后逐行 diff。

## 命中集比对结论

恰增 **2 条唯一命中**，全部为 SUB（表外边）：

| 命中码 | 命中明细 | 登记依据 |
| --- | --- | --- |
| IRD-GATE-SUB | 表外依赖边 ui->project（sdurws_ird_ui_app → sdurws_ird_project）不在 ARCH §3.5 白名单 | DTB §4.5 登记行（2026-09-22 生效）：装配层特权边——O-31 裁决"装配层同时看见两边写单行适配器"的链接面；打开五步协议适配（StoreFactoryPortAdapter/StorePortAdapter）经 ui 自有端口包装 project::ProjectStoreFactory/ProjectStore。**ui 产品库（sdurws_ird_ui）链接块与产品面源码零改动**——边只存在于装配层可执行目标 |
| IRD-GATE-SUB | 表外依赖边 ui->ui（sdurws_ird_ui_app → sdurws_ird_ui）不在 ARCH §3.5 白名单 | 同上登记行：app 目标链接被测单元产品库的自边——同型于既有 `sdurws_ird_ui_gui_test → sdurws_ird_ui`（face=other 形态 `->ui` 命中，UI-T14 已登记家族）；本目标被引擎分类为 app 形态（`_app` 后缀首次启用），故命中行呈现为 `ui->ui` |

其余各码（R-1/R-2/R-3/R-4/R-5/T-1/T-2/GRAPH/LIB/SA02）命中集与 base **逐行一致，零变化**。
特别地：

- **R-3 命中集零变化**：harness 源码位于 `ui/app/`（非 include/、src/ 产品扫描域），
  门禁产品面扫描未触碰；`_app` 形态不属于 R-3 链接面检查的 product/worker 集合
  （Qt 链接合法——与 `_plugin` 形态同口径）。
- **目标集合 diff**：恰增 1 个目标 `sdurws_ird_ui_app`（add_executable，非测试、
  不入 ctest 标签）；`sdurws_ird_ui` 等既有目标零变化。
- **DTB §4.5 登记册**：随本任务新增"SUB（ARCH §3.5 表外边）"行一条（2026-09-22），
  机器消费面（本文件）与人工登记册（DTB §4.5）一致。

## 责任方

登记册维护责任方：WP-01-T03（ird_gates 白名单与 DTB §4.5 双面一致性）。
本提交件为实施侧自登记（沿 UI-T13/UI-T14 同款流程），验收段核对。
