# WP-14 需求定义实施计划

**目标：** 将工程任务、区域、姿态约束、工艺段、负载事件和 Must/Should 规则编译为可执行 `EngineeringRequirements`。

**阶段/发布：** 阶段 B，R1；轨迹和动力字段只保存并校验，不在本 WP 执行完整动态求值。

**需求与契约：** REQ-01～08；AT-02、AT-03、AT-06、AT-18；引用 `architecture/domain-model.md`、`public-interfaces.md`、`testing-contract.md`。

**拥有目录：** `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/requirements/`、测试和 CSV 模板。通过 WP-04 命令服务提交，不直接写项目文件。

**输入/输出：** 输入为人工编辑、CSV 导入和模型作用域；输出为 `EngineeringRequirementsDraft`、就绪检查结果和诊断。所有任务、区域、负载和约束必须带 objectId、单位、来源和版本。

## 任务

1. **WP-14-T01 数据模型与单位校验**：定义任务点、姿态自由度、区域、工艺段、负载事件和约束字段；拒绝非有限值、非法单位和缺失引用。
2. **WP-14-T02 CSV 导入导出**：实现模板、字段映射、行级错误定位、公式注入防护和稳定往返；复用 WP-11 安全解析端口。
3. **WP-14-T03 姿态/区域语义**：实现部分位姿约束、4/5 轴任务子空间、边界包含规则和位置/姿态覆盖分母定义。
4. **WP-14-T04 负载与工艺事件**：实现驻留、接近/撤离、负载切换和完整任务循环语义；缺少关键数据时返回 DataInsufficient。
5. **WP-14-T05 就绪状态机**：实现草稿、Ready、Invalid、DataInsufficient 判定；非法 Must 不得进入计算，Should 只产生可见警告。
6. **WP-14-T06 项目命令集成**：应用需求修改只产生一个新修订；未应用草稿不使下游结果失效；撤销/重做经 WP-04 处理。
7. **WP-14-T07 阶段 B GUI**：实现批量编辑、筛选、单位显示、错误定位和就绪摘要；不显示内部 Schema/哈希作为主操作。

## 任务卡

详见 `agent-tasks/WP-14-T01-requirements-model.md`～`WP-14-T07-requirements-ui.md`。

## 验证

前置：WP-03、WP-04、WP-05、WP-10、WP-11、WP-13。

```powershell
pwsh -NoProfile -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_requirements_test$'
```

必须提交：CSV 往返样例、非法行报告、就绪判定矩阵、命令修订日志和 GUI 测试报告。

## 迁移与删除

旧需求格式只作为只读迁移输入；CSV 和旧适配器在阶段 B 验收后按删除清单移除。

## 独立评审

由需求负责人和独立测试人员复核字段、单位、就绪状态机和 CSV 安全证据。

## 退出条件

REQ-01～08、AT-02、AT-03、AT-18 阶段 B 子链路通过；所有 Must/Should 状态可观察；非法输入不产生修订；运动学可消费的执行工件版本化且可追溯。
