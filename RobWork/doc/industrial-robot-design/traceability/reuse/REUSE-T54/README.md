# REUSE-T54 原始记录：project.Codec JSON 解析家族迁移 Boost.JSON 渠道（P5/T05-project）

任务契约：`doc/industrial-robot-design/tasks/REUSE-T54.json`；起始 SHA `e21d617141607d2cf13ef3a4ca92325a9dfeaaf0`（工作区干净核实）；分支 `redesign-main` 主工作树串行。

## 1. 范围与设计判断（policy §1.1）

**保留**（逐项理由）：
- dump 侧 canonical 转义编码器（`appendJsonString`/`decodeUtf8At`/`appendUnicodeEscape`/`appendHexString`）——§4.8 输出恒为纯 ASCII（小写 \uXXXX/代理对）为产品冻结字节面契约，Boost.JSON 序列化器输出原生 UTF-8（探针 p7/p13 实证），不满足契约故不换；黄金字节用例（HeadRecord 字段序紧凑形态）与 NFR-COR-02 同值必同字节由此零风险保持；
- 错误分类与稳定码（StoreCorrupt/FormatLegacy/SchemaFuture、detail 前缀 `project/codec:`、SchemaFuture 升级指引三键 document=/supported=/upgrade=）；
- 身份字段 core 单源解析（`requireIdField<CoreId>`→`CoreId::tryFromCanonical`）——身份事实单源 core，零变化（红线）；
- payloadCanonical hex 文本编码（UI-T39）与 CR-02 唯一摘要入口 `contentVersionOf`（除它之外全文件零哈希计算）。

**删除**：`JsonValue` DOM、`findMember`、`JsonParser` 递归下降全族（parseValue/parseObject/parseArray/parseString/parseEscape/parseHex4/parseUnicodeEscape/appendUtf8/parseNumber 及 atEnd/peek/next/expect/printable）——约 17 矩阵行随批移除（矩阵行总览见 §5）。

**适配**：解析侧语义提取辅助（require*/integerInRange/optional*/parseRefPair/rejectUnknownFields/parsePreambleVersioned）以 `boost::json::value` 为载体，逻辑逐字段保持；`integerInRange` 适配 boost 的 int64/uint64 kind 分派（越界整数落 double kind→"must be an integer" 拒绝——探针 p6 实证；码语义不变）。

**新增（替代被删解析器）**：canonical 词法门 `scanCanonical` 族——单遍线性扫描自持 §4.8 词法 canonical 判定（拒绝非 ASCII 原始字节/裸控制字符/结构空白/浮点指数/true-false-null/前导零/重复键〔boost 坍缩重复键必须自持——p4 实证〕/孤立代理/深度>64），随后 `boost::json::parse`（显式 max_depth=64，其余默认＝标准 JSON 严格面）构建 DOM。Boost.JSON 仅承担 DOM 构建与结构/转义/整数解析（成熟第三方原语承担底层能力——core.Compare.closeWithin Boost 定宽算术同型先例）；词法门与全部磁盘契约语义归 project 自持；**model/状态 owner 不变**（纯函数，无共享状态）。

## 2. 授权渠道与三件套前置（REUSE-T43 byte-primitives-decision §5/§6）

- 渠道批准：所有者 2026-10-26 A4 裁决「Boost.Endian/Boost.JSON 渠道已批准——经 vcpkg 公共依赖渠道引入，不新增本仓 vendored 依赖」；boost-json 1.92.0 端口安装入既有 vcpkg installed 树（gitignore 核实不入库）。
- 字节基线（改动前捕获）：project_test 287/287＋contract_test 32/32 改动前全绿（`baseline/` 目录原件）——PersistenceFormatTest 黄金字节与拒绝矩阵即本包字节基线载体（JSON 场景无 reporting 式"无既有 fixture"缺口）。
- 逐用例对照：迁移后 project_test 290/290（287 既有＋3 新增通道钉扎）＋contract 32/32 全绿（`t54-final-*.log`）。
- 断开实验红转绿：`disconnect/`——词法门结构空白规则整体放行变异（两实现点同步）→ 锚点用例 `CodecMigrationChannel.WhitespaceInValidDocument_Rejected` 真实失败（exit 1）→ 恢复复验绿；变异绑定（原码/变异体治理 Digest 口径 sha256）见 `disconnect/disconnect-evidence.json`。

## 3. Boost.JSON 行为实证（probe/）

探针 15 项决策敏感行为（重复键坍缩/结构空白接受/浮点接受/越界整数 double kind/孤立代理拒绝/\u0000 保留/深度边界/serialize 原生 UTF-8 等）见 `probe/README.md`＋`probe/probe-run-output.log`＋源件 p4/p5/p6/p7/p11/p13/p14。首版探针 0xC0000409 事故（bash heredoc 经工具链 JSON 转义损坏 `\uXXXX`＋未捕获异常）如实记录于同 README，与库行为无关。

## 4. 验证分列（真实执行）

| 验证 | 结果 | 证据 |
| --- | --- | --- |
| 改动前基线 project_test | 287 passed / 0 failed / 0 skipped（exit 0） | baseline/project-test-baseline.log |
| 改动前基线 contract_test | 32 passed / 0 failed / 0 skipped（exit 0） | baseline/contract-test-baseline.log |
| 迁移后 project_test 全量 | 290 passed / 0 failed / 0 skipped（exit 0；48 套件） | t54-final-project-test.log＋.xml＋ird-test-report-project.json |
| 迁移后 contract_test 全量 | 32 passed / 0 failed / 0 skipped（exit 0） | t54-final-contract-test.log＋.xml |
| 身份解析回归 | PersistenceFormatTest.CoreContractConsumption.IdStrictness_ConsumedAsBaseline_PPR1＋BadCanonicalIdentity_Rejected_PPR1Baseline 含于上量全绿 | 同上 XML |
| 断开实验 | 红 exit 1（1 failed）→恢复绿 exit 0 | disconnect/ |
| ird_gates | 见契约 verify（双形态） | validate-docs 输出 |
| 独立验收 | 待所有者另开会话执行（实施者不自签） | — |

NOT RUN 项：无（本包验证面全部真实执行；性能/数值 oracle 不适用——无数值算法变更，纯序列化通道迁移）。

## 5. 矩阵与凭证

- `reuse/functions/project.json`：project.Codec.* 68 行 pending 全收口——51 行重锚 migrated（Dump/parse/词法提取/编码器族），17 行（JsonParser 全族＋findMember）随批删除；新增 canonical 词法门族行与全文件 audit 铺面行（Codec.cpp/Codec.hpp/新测试文件/project CMakeLists 全行覆盖）；inputs 摘要级联（Codec.cpp/Codec.hpp/project CMakeLists 变更面）。
- `reuse/functions/foundation-governance.json`：ird_gates_whitelist L1—L10 重锚（IRD_THIRDPARTY_ALLOWED_TARGETS 增登 "Boost::json"——Boost::headers REUSE-T19 先例同型）。
- `reuse/changes/REUSE-T54.json`：逐文件凭证（mode/business＋governance）。
- 父任务 REUSE-T05 scope 消费记录登记于该契约 note（父任务状态收口批归位）。
- units/project.md §1.4/§3.4/§4.8 与新依赖渠道同步（依赖契约修订登记）。

## 6. 已知边界与移交

- 词法门对"转义写法的同键"以解码后文本比对（与被删旧引擎解码比对语义一致，p13/p14 探针支撑）；
- boost 对 ≤INT64_MAX 的整数取 int64 kind、越界落 double kind——字段层统一拒绝（码不变），2^64/2^31 拒绝用例绿；
- 结构配对/成员逗号序等纯结构合法性由 boost 兜底（词法门最小自持集设计，诊断文案与旧引擎可能不同——码恒 StoreCorrupt，测试只认 code）；
- `\/` 转义与大写 hex `\u` 解析端容忍＝被删旧引擎同语义（dump 端不产生）；
- 探针基建事故（heredoc 转义/未捕获异常）与断开变异的单点陷阱教训记录于 probe/README 与 disconnect-evidence.json methodNote，供后续包复用。
