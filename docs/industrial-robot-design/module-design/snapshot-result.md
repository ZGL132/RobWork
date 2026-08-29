# 快照、结果与证据模块详细方案

- 方案版本：v0.2；需求基线：v0.7；负责 WP：WP-05；阶段/发布：阶段 A / R1
- 架构契约：`architecture/execution-model.md`、`architecture/persistence-schema.md`、`architecture/public-interfaces.md`、`architecture/testing-contract.md`

## 1. 模块职责与边界

本模块拥有评估输入切片、不可变分析快照、结果 envelope、证据 bundle、追加仓库和当前性索引。它不拥有项目修订写入权、评估算法、任务调度、GUI 或报告格式；结果文件通过 WP-04 的追加仓库能力保存，工作进程只能提交值对象。

## 2. 代码目录和构建

```text
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/evidence/
  include/sdurws/ird/evidence/
    EvaluatorDependencyManifest.hpp EvaluatorInputSlice.hpp
    AnalysisSnapshot.hpp EvidenceBundle.hpp ResultEnvelope.hpp
    ResultQuery.hpp IResultRepository.hpp ResultAdmission.hpp
    ResultCurrentnessService.hpp EvidenceDiagnostics.hpp
  src/DependencyResolver.cpp InputSlice.cpp AnalysisSnapshot.cpp
      ResultEnvelope.cpp ResultAdmission.cpp ResultRepository.cpp
      ResultCurrentnessService.cpp EvidenceJson.cpp
  test/InputSliceTest.cpp SnapshotTest.cpp ResultStatusTest.cpp
      ResultAdmissionTest.cpp ReportReadinessTest.cpp
  testdata/evidence/{slice,results,late,invalid}/
```

目标：`sdurws_ird_evidence`、`sdurws_ird_evidence_test`、`sdurws_ird_evidence_contract_test`。公共头只依赖 WP-03 core、WP-04 查询/追加接口、WP-06 名称接口和 Qt Core；禁止 Qt Widgets 和业务插件私有头。

## 3. 字段和序列化规则

`EvaluatorInputSlice` 的字段顺序固定为身份、模型内容、需求内容、策略、算法/目录版本、种子/线程、依赖字段、配置哈希和最终 `sliceHash`。依赖字段以 `path + contentIdentity + semanticRole` 表示，按 UTF-8 字节序排序；SHA-256 对规范 JSON（LF、无多余空白）计算。

`AnalysisSnapshot` 是深拷贝/值语义对象，包含 source revision、完整 slice、策略 ref、runtime name map ref、软件 baseline、seed、resource refs、创建时间和 snapshotId。任何 getter 返回值或 const 视图，不暴露可变容器。

`ResultEnvelope` 与 `EvidenceBundle` 分开保存：前者描述状态和 payload 身份，后者列出测试、资源保真度、诊断和人工签名。JSON 不写 NaN/Infinity；未知未来版本拒绝，已知升级由显式 registry 处理。

## 4. 依赖失效算法

依赖声明由评估器注册 `FieldDependency{fieldPath, semanticRole, invalidates[]}`。构建切片时只读取声明字段，规范化后计算哈希。比较旧/新切片时按 semanticRole 产生 `InvalidationReason` 列表，原因顺序稳定。TCP、工具质量、负载和策略按需求映射到相应评估器；显示开关、选择状态和名称拼写标记为 `NonPhysical`，不改变物理切片。

## 5. 结果接纳和当前性

接纳器依次校验 project/branch/revision、snapshotId/sliceHash、evaluator/version、policy、runtime name map、run/attempt、objectId 反解和状态组合。失败不写仓库。通过后追加一条不可变结果，并由当前性服务将同一项目/分支/评估器下旧兼容结果标记索引状态 `Superseded`；payload、证据和原始 JSON 不变。迟到结果仍追加到它携带的原分支历史，永远不提升为当前 revision 的结果。

## 6. 正式可行和报告就绪

调用 WP-03 的 `isFormallyFeasible`，不在本模块复制逻辑。模块只计算 `EvidenceGap{requirementId, missingEvaluator, missingResource, minimumLevel, actualLevel}`。缺少 RequiredEvidenceProfile、Quick 模式、Partial、DataInsufficient、NotEvaluated 或非 Current 结果均禁止进入正式报告；报告层消费缺口列表而不是猜测状态。

## 7. 测试与证据

单元测试覆盖字段排序、相同输入 hash 一致、无关字段不失效、状态笛卡尔积和证据等级。契约测试覆盖结果身份、迟到回调、重复 attempt、缓存排除、名称反解和查询过滤。测试证据记录输入 JSON、sliceHash、snapshotId、结果状态、诊断码、仓库 append 序号和独立评审者。

## 8. 迁移、扩展和评审

旧结果只读适配，无法验证完整来源时只能 EvidenceOnly。新增依赖角色、状态枚举或报告要求必须先改架构契约并提交 ADR。评审重点：切片是否覆盖全部影响因素、当前性是否只依赖声明字段、结果是否追加不覆盖、正式可行是否唯一、工作进程是否无法写项目 revision。
