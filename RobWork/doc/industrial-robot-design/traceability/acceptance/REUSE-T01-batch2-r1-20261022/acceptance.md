# REUSE-T01 第二批独立验收：FAIL

冻结SHA `4e2239f7672f367fff436ab83b7db56d49d19cfe`，基线 `849e03a321a1ce9642f13de4e907d9df2fb214a5`。独立临时clone最终tracked status为空；未实施、提交、推送或合入。

- **T01-B2-01 / P2** `RobWork/doc/industrial-robot-design/reuse/functions/io.json:9`：io.strictUtf8Check的唯一requirements=NFR-SEC-02。该需求只定义单文件大小、XML/压缩递归深度和解压量预算；Json.cpp145-162是字节DFA合法性检查，不包含预算。实际严格UTF-8依据是units/io.md§5.9.1（603行编码行）及IO-T04任务。应引用相关契约/规范或任务，避免把编码正确性当成资源预算实现证据；不需要修改权威需求。

- **T01-B2-02 / P2** `RobWork/doc/industrial-robot-design/reuse/catalog.json:1114`：GEOM-SPHERE-SAMPLING.search/resourceSearch.command将HyperSphere.*作为路径。PowerShell不展开该原生命令参数，rg报os error123并返回2，仅继续输出gtest命中，未搜索HyperSphere实现。使用显式HyperSphere.hpp/HyperSphere.cpp，或目录加-g HyperSphere.*。此问题与batch2-search.json的10条正确日志独立；后者重放全部匹配。

三项门禁及diff-check均exit=0。53条新增关系区段/input摘要、qualifiedSymbol/边界、编译target声明逐项核对；需求ID均存在，但上述UTF8条目语义追溯错误。3新增catalog能力的API/资源/版本/摘要/target均匹配，全部candidate。10条原始检索逐条重放，stdout、stderr、exitCode全部精确匹配；无命中的exit1没有被当成缺口或新核心许可。

重点反查：generateGridPositions是确定性体心枚举且capabilityId=NONE；RPY轴序没有宣称产品X/Y/Z字段可原序输入RobWork；sandbox目标真实且默认OFF；随机流、固定N球面、DH属性读取与拟合/差分/消元、姿态小核及高阶基函数均保持pending和适配差异，没有授权生产迁移。

范围仅第二批53新增关系和本批3候选/10检索；累计77关系涉及10单元仍全部pending，closedFiles=0，不是全20单元审计、生产复用或T02准入。无产品/上游/vcpkg变动，因此未重建C++或增加生产试验；完整上游测试、140门禁自测、DeepEvidence与远程CI未执行。原始检索文件未改写，重放输出单列search-*.stdout/stderr.txt，明细见acceptance.json。
