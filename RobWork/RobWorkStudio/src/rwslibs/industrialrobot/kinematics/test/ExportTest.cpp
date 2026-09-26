/**
 * @file   ExportTest.cpp
 * @brief  结果导出值行用例组（KinExport；WP-15-T09）——值行整理（解行/
 *         诊断行/覆盖行）、导出文件头来源声明与两分纪律、JSON/CSV 编码
 *         确定性与转义、导出零修订的结构面（KIN-08/AT-04/§7.4）。
 *
 * 设计依据：
 *   - units/kinematics.md §7.4（结果整理为值行（含 snapshotId/对象引用
 *     pointOid/regionOid）；来源在导出文件头声明；导出不产生修订 AT-04；
 *     写出经 io 通道——本组只验证值行数据整理面，写出器集成随 io 消费面
 *     验证〔任务契约 acceptance 2 原文口径〕）、§9.8（L-K10/导出命令）
 *   - REQUIREMENTS KIN-08（JSON/CSV 导出）、AT-04（导出不产生项目修订）、
 *     NFR-COR-01/02（同输入同字节）
 *   - 任务契约 tasks/foundation/WP-15-T09.json acceptance 2（具名对应见
 *     各用例 IRD_TEST_INFO 与用例名 _ACC2 段）
 */

#include <sdurws/ird/kinematics/Export.hpp>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记

#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

using namespace sdurws::ird::kinematics;

namespace core = sdurws::ird::core;

namespace {

// =====================================================================
// 测试夹具：固定身份与值行包（fromCanonical 严格解析——确定性，不用随机）
// =====================================================================

/// 固定快照内容身份（"cid-"＋64 hex——尾字节序号区分多个身份）。
core::ContentIdentity makeCid(unsigned char last)
{
    std::string text = "cid-";
    for (int i = 0; i < 63; ++i) {
        text += '0';
    }
    text += std::string{"0123456789abcdef"}[last % 16];
    return core::ContentIdentity::fromCanonical(text);
}

/// 固定任务点对象身份（"obj-"＋32 hex）。
core::ObjectId makeOid(unsigned char last)
{
    std::string text = "obj-";
    for (int i = 0; i < 31; ++i) {
        text += '0';
    }
    text += std::string{"0123456789abcdef"}[last % 16];
    return core::ObjectId::fromCanonical(text);
}

/// 手工构造一个解（导出行整理的来源值——零求解依赖）。
KinematicSolution handSolution(std::vector<double> q, double minMargin)
{
    KinematicSolution s;
    s.q = std::move(q);
    s.minimumJointMargin = minMargin;
    s.manipulability = 0.5;
    s.conditionNumber = 12.0;
    s.positionResidual = 1e-9;
    s.orientationResidual = 2e-9;
    s.sourceInitIndex = 3U;
    s.iterations = 42U;
    s.signature = "irdsig01deadbeef";
    return s;
}

/// 两解＋一碰撞诊断记录的点解集（解行/诊断行共用的来源值）。
IkSolutionSet makeSolutionSet()
{
    IkSolutionSet set;
    set.requestIdentity.snapshotId = makeCid(0x1);
    set.requestIdentity.sliceId = makeCid(0x2);
    set.targetRef.pointOid = makeOid(0xa);
    set.targetRef.conditionId = makeOid(0xb);
    set.solutions.push_back(handSolution({0.1, 0.2}, 0.9));
    set.solutions.push_back(handSolution({0.3, 0.4}, 0.7));
    set.solutions[0].collisionStatus.evaluated = true;
    set.solutions[1].collisionStatus.evaluated = true;

    // 一条碰撞过滤记录（原因＝碰撞、两个对象对——诊断行的承载源）。
    FilteredSolutionRecord rec;
    rec.q = {0.5, 0.6};
    rec.reason = SolutionFilterReason::Collision;
    rec.positionResidual = 1e-8;
    rec.orientationResidual = 2e-8;
    rec.minimumJointMargin = 0.1;
    rec.manipulability = 0.4;
    rec.objectIdPairs = {makeOid(0xc), makeOid(0xd), makeOid(0xe), makeOid(0xf)};
    rec.sourceInitIndex = 5U;
    rec.iterations = 17U;
    rec.signature = "irdsig01cafebabe";
    set.filteredRecords.push_back(rec);
    return set;
}

/// 两区域×双口径的样本集与结果（覆盖行 tally 的来源值）：
///   区域 A（oid 0x1）：位置样本 3（2 达 1 数据不足）＋位姿样本 1（未达）；
///   区域 B（oid 0x2）：位置样本 1（未运行——取消残留）。
RegionCoverageComputation makeCoverageComputation()
{
    RegionCoverageComputation comp;
    comp.snapshotId = makeCid(0x1);
    comp.sliceId = makeCid(0x2);

    const core::ObjectId regionA = makeOid(0x1);
    const core::ObjectId regionB = makeOid(0x2);

    // 样本（全局序连续——分母完整性前提；首现序 A 先 B 后）。
    struct Spec {
        core::ObjectId oid;
        SampleKind kind;
        SampleState state;
    };
    const Spec specs[] = {
        {regionA, SampleKind::Position, SampleState::Reached},
        {regionA, SampleKind::Position, SampleState::Reached},
        {regionA, SampleKind::Position, SampleState::DataInsufficient},
        {regionA, SampleKind::Pose, SampleState::Unreachable},
        {regionB, SampleKind::Position, SampleState::NotRun},
    };
    for (std::uint64_t i = 0; i < 5; ++i) {
        SampleRecord rec;
        rec.sampleIndex = i;
        rec.regionObjectId = specs[i].oid;
        rec.kind = specs[i].kind;
        comp.samples.samples.push_back(rec);

        SampleResultRecord res;
        res.sampleIndex = i;
        res.state = specs[i].state;
        comp.results.results.push_back(res);
    }
    return comp;
}

/// 归档来源的导出文件头（runId 非空——两分纪律的合法形态）。
KinExportHeader archivedHeader()
{
    KinExportHeader h;
    h.source = KinExportSource::ArchivedRun;
    h.runId = "run-0001";
    h.snapshotId = makeCid(0x1);
    h.mode = core::EvaluationMode::Verified;
    return h;
}

}  // namespace

// ---------------------------------------------------------------------
// ACC2-1：解行/诊断行整理——snapshotId/pointOid/conditionId 逐行携带
// ---------------------------------------------------------------------

TEST(KinExport, SolutionRowsCarryIdentityAndObjectRefs_WP15T09_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-08"},
                  std::vector<std::string>{});

    const IkSolutionSet set = makeSolutionSet();
    const auto rows = buildSolutionExportRows(set);

    // 行数＝解数；rank＝稳定序下标（生产端已排序——整理面不重排）。
    ASSERT_EQ(rows.size(), 2U);
    EXPECT_EQ(rows[0].rank, 0U);
    EXPECT_EQ(rows[1].rank, 1U);
    for (const KinSolutionExportRow& r : rows) {
        // 逐行携带快照身份与对象引用（§7.4"含 snapshotId/对象引用"——
        // CSV 消费方无需回查文件头即可溯源）。
        EXPECT_EQ(r.snapshotId, set.requestIdentity.snapshotId);
        EXPECT_EQ(r.pointOid, set.targetRef.pointOid);
        ASSERT_TRUE(r.conditionId.has_value());
        EXPECT_EQ(*r.conditionId, *set.targetRef.conditionId);
    }
    // 指标透传（整理≠再计算——值面同源）。
    EXPECT_EQ(rows[0].minimumJointMargin, 0.9);
    EXPECT_EQ(rows[1].minimumJointMargin, 0.7);
    EXPECT_EQ(rows[0].q, (std::vector<double>{0.1, 0.2}));
    EXPECT_TRUE(rows[0].collisionEvaluated);
    EXPECT_FALSE(rows[0].collisionInCollision)
        << "解行恒无碰撞标记（生产端不变式——碰撞解在诊断行）";
}

TEST(KinExport, FilteredRowsCarryReasonTokenAndCollisionPairs_WP15T09_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-08"},
                  std::vector<std::string>{});

    const IkSolutionSet set = makeSolutionSet();
    const auto rows = buildFilteredSolutionExportRows(set);

    ASSERT_EQ(rows.size(), 1U);
    EXPECT_EQ(rows[0].reason, "collision") << "Collision→词表 token 一一映射";
    EXPECT_EQ(rows[0].collisionPairCount, 4U);
    // 对象对成对展平为 canonical 文本（竖线分隔——R-4：身份直出无拼接）。
    EXPECT_EQ(rows[0].collisionPairs,
              makeOid(0xc).toCanonical() + "|" + makeOid(0xd).toCanonical() + "|"
                  + makeOid(0xe).toCanonical() + "|" + makeOid(0xf).toCanonical());
    EXPECT_EQ(rows[0].snapshotId, set.requestIdentity.snapshotId);
    EXPECT_EQ(rows[0].sourceInitIndex, 5U);

    // 原因 token 三值全表（导出面词表唯一书写的映射完整性）。
    FilteredSolutionRecord rec;
    IkSolutionSet probe = set;
    probe.filteredRecords.clear();
    rec.reason = SolutionFilterReason::ResidualRecheck;
    probe.filteredRecords.push_back(rec);
    EXPECT_EQ(buildFilteredSolutionExportRows(probe)[0].reason,
              "residual-recheck");
    probe.filteredRecords[0].reason = SolutionFilterReason::JointLimit;
    EXPECT_EQ(buildFilteredSolutionExportRows(probe)[0].reason, "joint-limit");
}

// ---------------------------------------------------------------------
// ACC2-2：覆盖行 tally——(regionOid, kind) 分组＋双射守卫
// ---------------------------------------------------------------------

TEST(KinExport, CoverageRowsTallyByRegionAndKind_WP15T09_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-08", "KIN-04"},
                  std::vector<std::string>{});

    const RegionCoverageComputation comp = makeCoverageComputation();
    const auto rows = buildCoverageExportRows(comp);

    // 行序＝(regionOid 首现序, position→pose)；零样本口径不产行
    // （区域 B 无位姿样本）。
    ASSERT_EQ(rows.size(), 3U);
    EXPECT_EQ(rows[0].regionOid, makeOid(0x1));
    EXPECT_EQ(rows[0].kind, "position");
    EXPECT_EQ(rows[0].planned, 3U);
    EXPECT_EQ(rows[0].reached, 2U);
    EXPECT_EQ(rows[0].dataInsufficient, 1U);
    EXPECT_EQ(rows[1].regionOid, makeOid(0x1));
    EXPECT_EQ(rows[1].kind, "pose");
    EXPECT_EQ(rows[1].planned, 1U);
    EXPECT_EQ(rows[1].unreachable, 1U);
    EXPECT_EQ(rows[2].regionOid, makeOid(0x2));
    EXPECT_EQ(rows[2].kind, "position");
    EXPECT_EQ(rows[2].planned, 1U);
    EXPECT_EQ(rows[2].notRun, 1U);
    // 逐行携带快照与区域引用（regionOid——§7.4 对象引用面）。
    for (const KinCoverageExportRow& r : rows) {
        EXPECT_EQ(r.snapshotId, comp.snapshotId);
    }
}

TEST(KinExport, CoverageRowsBijectionViolationFailsFast_WP15T09_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-08"},
                  std::vector<std::string>{});

    // 缺结果记录（数量不等）→ logic_error（§7.2 双射前提——导出面不
    // 静默修补）。
    RegionCoverageComputation missing = makeCoverageComputation();
    missing.results.results.pop_back();
    EXPECT_THROW(buildCoverageExportRows(missing), std::logic_error);

    // 重复 sampleIndex（双射第二出现）→ logic_error。
    RegionCoverageComputation duplicated = makeCoverageComputation();
    duplicated.results.results.push_back(duplicated.results.results.front());
    EXPECT_THROW(buildCoverageExportRows(duplicated), std::logic_error);
}

// ---------------------------------------------------------------------
// ACC2-3：文件头来源两分纪律（§7.4——来源在导出文件头声明且自洽）
// ---------------------------------------------------------------------

TEST(KinExport, HeaderSourceDisciplineFailsFast_WP15T09_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-08", "AT-04"},
                  std::vector<std::string>{});

    // 归档来源缺 runId → invalid_argument（来源声明不完整）。
    KinExportHeader noRun = archivedHeader();
    noRun.runId.clear();
    EXPECT_THROW(buildKinExportPackage(noRun, {}, {}, {}), std::invalid_argument);

    // 会话内存来源带 runId → invalid_argument（会话结果无 run——矛盾
    // 声明不得落盘）。
    KinExportHeader ghostRun = archivedHeader();
    ghostRun.source = KinExportSource::SessionMemory;
    EXPECT_THROW(buildKinExportPackage(ghostRun, {}, {}, {}), std::invalid_argument);

    // 两分合法形态照常组装（对照组）。
    EXPECT_NO_THROW(buildKinExportPackage(archivedHeader(), {}, {}, {}));
    KinExportHeader session = archivedHeader();
    session.source = KinExportSource::SessionMemory;
    session.runId.clear();
    EXPECT_NO_THROW(buildKinExportPackage(session, {}, {}, {}));
}

// ---------------------------------------------------------------------
// ACC2-4：JSON 编码——来源/身份/模式声明＋确定性同字节
// ---------------------------------------------------------------------

TEST(KinExport, JsonEncodeDeclaresSourceAndIsDeterministic_WP15T09_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-08", "NFR-COR-01", "AT-04"},
                  std::vector<std::string>{});

    const IkSolutionSet set = makeSolutionSet();
    const RegionCoverageComputation comp = makeCoverageComputation();
    const KinExportPackage pkg = buildKinExportPackage(
        archivedHeader(), buildSolutionExportRows(set),
        buildFilteredSolutionExportRows(set), buildCoverageExportRows(comp));

    const std::string json = encodeKinExportJson(pkg);
    // 文件头声明面（§7.4）：来源/快照/模式逐字段在场。
    EXPECT_NE(json.find("\"format\": \"ird-kin-export\""), std::string::npos);
    EXPECT_NE(json.find("\"source\": \"archived-run\""), std::string::npos);
    EXPECT_NE(json.find("\"runId\": \"run-0001\""), std::string::npos);
    EXPECT_NE(json.find("\"snapshotId\": \"" + makeCid(0x1).toCanonical() + "\""),
              std::string::npos);
    EXPECT_NE(json.find("\"mode\": \"verified\""), std::string::npos);
    // 对象引用面：pointOid/regionOid 逐行在场。
    EXPECT_NE(json.find(makeOid(0xa).toCanonical()), std::string::npos);
    EXPECT_NE(json.find(makeOid(0x1).toCanonical()), std::string::npos);
    // 值行三段（空段输出 []——形状稳定）。
    EXPECT_NE(json.find("\"solutions\": ["), std::string::npos);
    EXPECT_NE(json.find("\"filteredSolutions\": ["), std::string::npos);
    EXPECT_NE(json.find("\"coverage\": ["), std::string::npos);

    // 确定性：同一包两次编码逐字节一致（NFR-COR-01）。
    EXPECT_EQ(encodeKinExportJson(pkg), json);

    // AT-04 结构面：编码前后包内容不变（纯函数——无隐藏状态、无修订
    // 通道；类型面不存在项目/命令依赖）。
    const std::string again = encodeKinExportJson(pkg);
    EXPECT_EQ(again, json);
}

// ---------------------------------------------------------------------
// ACC2-5：CSV 编码——'#' 注释头＋三段表＋RFC 4180 转义
// ---------------------------------------------------------------------

TEST(KinExport, CsvEncodeHeaderSectionsAndEscaping_WP15T09_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-08", "NFR-COR-01"},
                  std::vector<std::string>{});

    const IkSolutionSet set = makeSolutionSet();
    const RegionCoverageComputation comp = makeCoverageComputation();
    const KinExportPackage pkg = buildKinExportPackage(
        archivedHeader(), buildSolutionExportRows(set),
        buildFilteredSolutionExportRows(set), buildCoverageExportRows(comp));

    const std::string csv = encodeKinExportCsv(pkg);
    // 注释头（来源声明——逐行定序）。
    EXPECT_NE(csv.find("# ird-kin-export format=csv version=1\n"), std::string::npos);
    EXPECT_NE(csv.find("# source: archived-run\n"), std::string::npos);
    EXPECT_NE(csv.find("# run-id: run-0001\n"), std::string::npos);
    EXPECT_NE(csv.find("# snapshot-id: " + makeCid(0x1).toCanonical() + "\n"),
              std::string::npos);
    // 三段表（列头行在场——空段也保持形状）。
    EXPECT_NE(csv.find("# section: solutions\n"), std::string::npos);
    EXPECT_NE(csv.find("# section: filtered-solutions\n"), std::string::npos);
    EXPECT_NE(csv.find("# section: coverage\n"), std::string::npos);
    // 浮点最短往返词形（0.1/0.2 → "0.1"/"0.2"——非格式化重影）。
    EXPECT_NE(csv.find(",0.1,0.2,"), std::string::npos);
    // 确定性：同包两次编码逐字节一致。
    EXPECT_EQ(encodeKinExportCsv(pkg), csv);

    // 转义面（RFC 4180）：含逗号的字符串字段加引号、内部引号加倍。
    IkSolutionSet weird = makeSolutionSet();
    weird.solutions[0].signature = "has,comma\"quote";
    KinExportPackage weirdPkg = buildKinExportPackage(
        archivedHeader(), buildSolutionExportRows(weird), {}, {});
    const std::string weirdCsv = encodeKinExportCsv(weirdPkg);
    EXPECT_NE(weirdCsv.find("\"has,comma\"\"quote\"\n"), std::string::npos)
        << "含逗号/引号字段必须按 RFC 4180 转义";
}

TEST(KinExport, SessionMemoryHeaderOmitsRunIdLine_WP15T09_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-08"},
                  std::vector<std::string>{});

    KinExportHeader session = archivedHeader();
    session.source = KinExportSource::SessionMemory;
    session.runId.clear();
    const KinExportPackage pkg = buildKinExportPackage(session, {}, {}, {});

    const std::string csv = encodeKinExportCsv(pkg);
    EXPECT_NE(csv.find("# source: session-memory\n"), std::string::npos);
    EXPECT_EQ(csv.find("# run-id:"), std::string::npos)
        << "会话内存来源不携带 run-id 行（两分纪律的文本面）";
    const std::string json = encodeKinExportJson(pkg);
    EXPECT_EQ(json.find("\"runId\""), std::string::npos)
        << "会话内存来源不携带 runId 字段";
}
