# WP-13-T15 ird_gates 引擎直跑归一化比对（base f3b87dfb → head）

- 比对口径：T12/T13/T14 确立口径——引擎直跑（cmake -P）base..head 归一化
  （IRL 根路径剥离为 `<IRD_ROOT>`）后逐行比对，存量命中不计、新增命中须为零
  或逐条具名登记。
- 数据文件：`ird_gates-base-normalized.txt`（56 条）/ `ird_gates-head-normalized.txt`（58 条）。

## 比对结论

归一化后 head 相对 base **恰增 2 处命中**，均由本任务 acceptance 1 明文
规定的插件链接面（卡 §3.2："插件目标 `sdurws_ird_modeling_plugin` →
`sdurws_ird_modeling`＋`sdurws_ird_ui`"）经引擎文本解析产生：

1. `IRD-GATE-R1: 业务域目标互链：sdurws_ird_modeling_plugin → sdurws_ird_modeling`
   ——插件目标链接本单元计算库的**自边**。引擎的 R-1 判定按"两端均属
   IRD_BUSINESS_UNITS"粗粒度归类，同单元自边与业务域互链共用同一码面；
   R-1 红线语义（业务域单元**之间**互链禁止）不含同单元自边——本边即
   卡 §3.2 二分结构"插件消费计算库"的既有 sanctioned 面。
2. `IRD-GATE-SUB: 表外依赖边 modeling->ui（sdurws_ird_modeling_plugin → sdurws_ird_ui）`
   ——插件目标链接 ui 公共面（acceptance 1 明文链接面；IPluginUiRegistrar/
   CommandDescriptor 等装配面消费）。表外边登记册（DTB §4.5）回填归
   WP-01-T03 治理面——本任务 allowedFiles 不含
   `cmake/ird_gates_whitelist.cmake` 与 `development-task-breakdown.md`。

两处与 UI-T15/T16 的 `ui->ui`／`ui->project` 两行（DTB §4.5 既有登记行，
"插件目标链接本单元产品库自边，同型于 ui_app 既有形态"）同型同源，均为
装配层目标的既定登记模式。**除此之外零新增命中**（R-1/R-2/R-3/R-4/R-5/
T-1/T-2/SUB/LIB 全码面）。

## 登记随附说明

- 构建图契约测试（BuildGraphContractTest）的 T02 落位期断言已随本落位
  做合法随附同步（_plugin 出现断言翻转＋ui 边行级钉住），登记于
  units/modeling.md §14.6 v0.17。
- 治理面待办（非本任务辖域）：DTB §4.5 增行＋whitelist 增
  "modeling->modeling"/"modeling->ui" 机器面（若按 ui 先例仅登记 §4.5
  手册面则两处为登记存量命中）。
