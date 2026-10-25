# REUSE-T45 — WP-19 批次四：reporting 单元审计收口

起始 SHA `4d9ac4777d833af5eb0efdea6eabe5ace77ccd75`（批次三 scope 修正提交）。状态：实施完成待独验。**零产品变更**。

## 判定与收口

- **ReportCodec.cpp 64 行保持 pending（AD 结构墙，T36/T41 结构同型）**：该文件存在真实受限类别命中——ReportCodec.cpp:127 处 `const rw::kinematics::State* state = m_deps.currentState();`（快照/时间面借用 State 只读指针）；REUSE-AD 对已解决记录要求精确 file/target/operation grant，而权限表无 reporting 授权（权限表 11 项 grant 全无 reporting；reporting 非模型 owner）。行保持 pending＝登记债务不洗绿；解阻＝所有者批准 reporting 借用类 grant（origin 审查），届时按字节原语族决策记录（REUSE-T43/byte-primitives-decision.md）处置——Boost.Endian 批准前置＝先捕获字节基线（评审缺口 2：reporting 无既有字节 fixture，testdata/golden 29 家族无 rpt-*，顺序敏感）。
- **Archive.cpp publish（UTIL-FILE，1 行）**：报告归档发布为产品证据契约编排（版本/目录/原子面），上游 IOUtil 通用原语不承载——business-specific；publish 写归档不写 project 修订，project 唯一修订写权不受影响。
- **构建宏（TEST-HARNESS，1 行）**：REUSE-T32 ird_add_gtest 定案同族保留。

## 矩阵与凭证

- functions/reporting.json：66 行 pending → 2 行 business-specific migrated ＋ 64 行保持 pending（债务登记）；连同 2 行既有 resolved 共 68 行。
- nonoverlap-reporting.json：逐 functionId 条目（族判定＋行差异引用＋结构墙说明）。
- changes/REUSE-T45.json：空产品变更清单。search-batch.txt：S1/S2 检索记录（S2 核实受限类别命中源）。
