# WP-14-T12 第四批：CSV生产映射与批次撤销

CSV增加手动列映射和每字段单位声明，同源previewUnitConversion实时显示SI数值；缺必填映射或单位非法时不能下一步。确认页完整呈现行号、列名、原文、原因及建议，包含忽略列、默认字段和正确行数值，可滚动复制，不解释富文本。

正确行生成各自临时ObjectId并以一次EditBatch确认；解决原先全零身份多行互相覆盖与逐行撤销。冲突整批拒绝，原因可见；无正确行不制造空编辑。批次接受经私有回调记录一次面板撤销，真实“撤销/重做”按钮可一次恢复整个导入，模板/阵列共用该回调。

真实验证（VS2022 x64/windows/绝对exe/GUI串行）：

- csv-red-model与csv-red-gui：原多行覆盖/非原子撤销、无映射页及错误截断实际失败。csv-undo-red：新增真实按钮断言复现导入后撤销仍disabled。保留原始XML/JSON/日志。
- csv-first-green及csv-undo-green：对应修复回归通过。
- batch4-integrated：模型215通过+1旧占位跳过、契约16通过、GUI62通过+1失败；失败系新增夹具漏设hasHeader，未进入产品映射页。修正夹具后重跑，不覆盖旧证据。
- batch4-integrated-green及batch4-standalone：每种295跑，294通过+1旧占位跳过、0失败（模型215通过+1跳过、契约16通过、GUI63通过）。覆盖mm/deg、12条错误、取消、映射缺列/非法单位、重名整批拒绝、按钮整体撤销/重做。
- 集成相关目标/正式宿主/ird_gates构建退出0，独立三目标构建退出0；构建原始日志见batch4-build。

唯一跳过GuiRegistration.V22RegisteredNotExecuted_ACC1是旧模型目标中的GUI登记占位，不代表功能验收通过。以上为组件/模型验证；正式宿主文件对话和原120项三维/联合验收继续执行，不能从这些数字推导全功能通过。
