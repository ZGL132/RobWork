/**
 * @file   KinHostServicesAssemblyTest.cpp
 * @brief  kinematics 面板服务缝宿主装配的测试（★ KIN-P4——审查报告 §4
 *         第一条"降级服务逐项接通"的具名自证面：命令门面网关转译／
 *         根对象补丁缝／导出写出（io 原子真实写出）／求解配置持久化
 *         保存-恢复闭环）。
 *
 * 设计依据：
 *   - 被测 TU 同源编入（plugin/KinHostServicesAssembly.cpp——ui_test
 *     守卫块，HostCompilePort/KinEvaluationChannel 先例形态）；
 *   - 测试面直链登记：sdurws_ird_project（IProjectQueryPort 值面——网关
 *     转译语义自证）＋sdurws_ird_io（AtomicFile 真实写出——L-K10 写出
 *     半区产品级自证），登记册 traceability/kin-p4-gate-registrations.md；
 *   - 审查条目映射：§4 第一条（命令门面/导出/配置持久化三缝从"通道未
 *     装配"变为"宿主产品物注入"）。
 *
 * 线程约束：QCoreApplication 级（TestMainReport 构造）——全部产品入口
 *   在 main 线程直调（面板缝纪律的测试镜像）。
 */

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/审查条目追溯

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/kinematics/AnalysisConfig.hpp>  // 合法配置基线（I-KIN-4）
#include <sdurws/ird/kinematics/DiagCodes.hpp>
#include <sdurws/ird/project/CommandService.hpp>
#include <sdurws/ird/project/QueryPort.hpp>

#include "plugin/KinHostServicesAssembly.hpp"  // 被测装配半区（同源直编 TU）

// ---- 使用声明（与被测头同一命名空间——用例可读性）--------------------
using namespace sdurws::ird;
using namespace sdurws::ird::ui;
namespace kin = sdurws::ird::kinematics;
namespace prj = sdurws::ird::project;

namespace {

// =====================================================================
// 固定身份派生（KinFkFixture 先例的自持最小副本——固定种子确定性）
// =====================================================================

core::Digest256 digestOf(const std::string& seed)
{
    core::ContentDigester d;
    d.update(seed.data(), seed.size());
    return d.finalize();
}

template <typename Id>
Id idFrom(const std::string& seed)
{
    const core::Digest256 d = digestOf(seed);
    Id id;
    std::copy(d.begin(), d.begin() + 16, id.bytes.begin());
    return id;
}

core::ContentVersion cvFrom(const std::string& seed)
{
    core::ContentVersion cv;
    cv.bytes = digestOf(seed);
    return cv;
}

// =====================================================================
// FakeQueryPort——IProjectQueryPort 最小替身（网关基线取数的供数面；
// 非被测方法如实 fail——替身只实现网关消费路径）
// =====================================================================

class FakeQueryPort final : public prj::IProjectQueryPort {
public:
    prj::RevisionView head() const override { return m_head; }
    std::optional<prj::RevisionView> tryRevision(core::RevisionId) const override
    {
        ADD_FAILURE() << "替身未实现 tryRevision（网关零消费路径）";
        return std::nullopt;
    }
    prj::RevisionView revision(core::RevisionId) const override
    {
        ADD_FAILURE() << "替身未实现 revision（网关零消费路径）";
        return {};
    }
    prj::ProjectMetadataView currentMetadata() const override
    {
        ADD_FAILURE() << "替身未实现 currentMetadata（网关零消费路径）";
        return {};
    }
    std::optional<prj::ProjectMetadataView> metadataAt(
        core::RevisionId) const override
    {
        ADD_FAILURE() << "替身未实现 metadataAt（网关零消费路径）";
        return std::nullopt;
    }
    std::vector<prj::BranchTip> branchTips() const override
    {
        return m_tips;
    }
    std::vector<prj::RevisionView> branchHistory(
        core::BranchId, std::uint32_t) const override
    {
        ADD_FAILURE() << "替身未实现 branchHistory（网关零消费路径）";
        return {};
    }
    std::optional<std::vector<std::uint8_t>> tryObject(
        core::ObjectId oid, core::ContentVersion cv) const noexcept override
    {
        // 引用存在性语义（§4.6——引用表内（oid+cv 对上）才返回字节；
        // noexcept try 轨——失败一律 nullopt 不抛）。
        for (const prj::ObjectRef& ref : m_head.objectRefs) {
            if (ref.objectId == oid && ref.contentVersion == cv) {
                return m_bytes;
            }
        }
        return std::nullopt;
    }
    std::vector<std::uint8_t> object(core::ObjectId,
                                     core::ContentVersion) const override
    {
        ADD_FAILURE() << "替身未实现 object（网关零消费路径）";
        return {};
    }
    std::vector<prj::DraftInfo> listDrafts(core::BranchId) const override
    {
        ADD_FAILURE() << "替身未实现 listDrafts（网关零消费路径）";
        return {};
    }
    std::vector<prj::RunInfo> listRuns(core::RevisionId) const override
    {
        ADD_FAILURE() << "替身未实现 listRuns（网关零消费路径）";
        return {};
    }
    std::filesystem::path runDir(core::RunId) const override
    {
        ADD_FAILURE() << "替身未实现 runDir（网关零消费路径）";
        return {};
    }

    prj::RevisionView m_head;                       ///< 预置 HEAD 视图
    std::vector<prj::BranchTip> m_tips;             ///< 预置分支表（单默认分支——INV-M3）
    std::vector<std::uint8_t> m_bytes;              ///< 根对象字节（robot-design——透传消费）
};

/// 预置"一分支一根对象"的替身（网关 fetchBaseline 的黄金路径供数）。
FakeQueryPort makeSeededPort()
{
    FakeQueryPort port;
    prj::BranchTip tip;
    tip.id = idFrom<core::BranchId>("host-brn");
    tip.tip = idFrom<core::RevisionId>("host-rev-1");
    port.m_tips.push_back(tip);

    prj::ObjectRef ref;
    ref.objectId = idFrom<core::ObjectId>("host-root");
    ref.contentVersion = cvFrom("host-root-cv");
    ref.objectTypeToken = kin::kRobotDesignObjectTypeToken;  // 同串金标（modeling kRobotDesignObjectType）
    port.m_head.objectRefs.push_back(ref);
    port.m_bytes = {0x01, 0x02, 0x03, 0x04};  // 不透明透传——网关零解码
    return port;
}

/// 合法求解配置基线（I-KIN-4 seed≥1——与产品装配纪律同则）。
kin::AnalysisConfiguration makeLegalConfig()
{
    kin::AnalysisConfiguration config;
    config.seed = 7;
    config.regionBudget.seed = 9;
    config.initialValuesCount = 4;
    return config;
}

}  // namespace

// =====================================================================
// 命令门面网关（★ KIN-P4 审查 §4"命令门面"接通的自证面）
// =====================================================================

/// fetchBaseline 黄金路径：分支/tip/根对象身份与字节三事实齐备（kinematics
/// 门面组装 apply-robot-design 载荷的全部输入）；修复前宿主无网关注入，
/// 设默认命令门面恒缺位（按钮禁用降级）。
TEST(KinHostServicesAssembly, Gateway_FetchBaselineGoldenPath_KIN_P4)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-14"}, std::vector<std::string>{"AT-27"});
    FakeQueryPort port = makeSeededPort();
    int submitted = 0;
    KinProjectCommandGateway gateway(
        &port, [&submitted](prj::CommandEnvelope) {
            ++submitted;
            return prj::CommandResult{};  // 闭包零消费（本用例只取基线）
        });

    const auto baseline = gateway.fetchBaseline();
    ASSERT_TRUE(baseline.ok());
    EXPECT_EQ(baseline.get().branch, port.m_tips.front().id);
    EXPECT_EQ(baseline.get().tipRevision.has_value(), true);
    EXPECT_EQ(*baseline.get().tipRevision, port.m_tips.front().tip);
    ASSERT_TRUE(baseline.get().rootObjectId.has_value());
    EXPECT_EQ(*baseline.get().rootObjectId,
              port.m_head.objectRefs.front().objectId);
    EXPECT_EQ(baseline.get().rootObjectBytes,
              (std::vector<std::uint8_t>{0x01, 0x02, 0x03, 0x04}));
    EXPECT_EQ(submitted, 0);  // 基线取数零提交（②端口只读语义）
}

/// fetchBaseline 无根拒绝：闭包无 robot-design 根＝NoDevice 错误轨（§9.6
/// KIN-NO-DEVICE——"无可用设备"业务出口，值面返回不抛异常）。
TEST(KinHostServicesAssembly, Gateway_FetchBaselineNoDeviceTrack_KIN_P4)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-14"}, std::vector<std::string>{});
    FakeQueryPort port = makeSeededPort();
    port.m_head.objectRefs.clear();  // 闭包无根
    KinProjectCommandGateway gateway(&port, [](prj::CommandEnvelope) {
        return prj::CommandResult{};
    });

    const auto baseline = gateway.fetchBaseline();
    ASSERT_FALSE(baseline.ok());
    EXPECT_EQ(baseline.error().code, kin::KinematicsErrorCode::NoDevice);
}

/// submit 转译：KinCommandEnvelope → project::CommandEnvelope 逐字段直投
/// （分支/期望基线/token/版本/载荷字节），Committed 回显携带新修订——
/// 修复前该路径不存在（通道未定义），设默认 TCP 无法产生修订。
TEST(KinHostServicesAssembly, Gateway_SubmitTranslatesEnvelopeAndEchoes_KIN_P4)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-14"}, std::vector<std::string>{"AT-27"});
    FakeQueryPort port = makeSeededPort();
    std::optional<prj::CommandEnvelope> captured;
    KinProjectCommandGateway gateway(
        &port, [&captured](prj::CommandEnvelope envelope) {
            captured = std::move(envelope);
            prj::CommandResult committed;
            committed.status.kind = prj::CommandStatus::Kind::Committed;
            committed.newRevision = idFrom<core::RevisionId>("host-rev-2");
            return committed;
        });

    kin::KinCommandEnvelope in;
    in.branch = port.m_tips.front().id;
    in.expectedRevision = port.m_tips.front().tip;
    in.commandType = std::string(kin::kFacadedModelingCommand);
    in.payloadFormatVersion = kin::kModelingCommandPayloadVersion;
    in.payloadCanonical = {0x0A, 0x0B};
    const kin::CommandSubmission out = gateway.submit(in);

    ASSERT_TRUE(captured.has_value());
    EXPECT_EQ(captured->branch, in.branch);
    EXPECT_EQ(captured->expectedRevision, in.expectedRevision);
    EXPECT_EQ(captured->commandType, std::string("apply-robot-design"));
    EXPECT_EQ(captured->payloadFormatVersion, 2U);
    EXPECT_EQ(captured->payloadCanonical, in.payloadCanonical);
    ASSERT_TRUE(out.committed());
    ASSERT_TRUE(out.newRevision.has_value());
    EXPECT_EQ(*out.newRevision, idFrom<core::RevisionId>("host-rev-2"));
}

// =====================================================================
// 根对象补丁缝（modeling 编解码权威保持——宿主零第二套编解码器）
// =====================================================================

/// 补丁缝错误轨：非 canonical 字节解码失败＝nullopt（门面转
/// KIN-ROOT-BYTES-ILLEGAL 诊断零提交——语义单点在 KinematicsCommandHandler）。
TEST(KinHostServicesAssembly, PatchRootDesign_IllegalBytesNullOpt_KIN_P4)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-14"}, std::vector<std::string>{});
    kin::TcpRef tcp;
    tcp.toolObject = idFrom<core::ObjectId>("patch-tool");
    tcp.tcpKey = "tcp_1";
    const std::vector<std::uint8_t> garbage = {0xDE, 0xAD, 0xBE, 0xEF};
    EXPECT_FALSE(
        patchRootDesignDefaultTcp(garbage, tcp).has_value());
}

// =====================================================================
// 导出写出缝（★ KIN-P4 审查 §4"导出"接通面——io AtomicFile 真实写出）
// =====================================================================

/// 真实写出闭环：临时目录＋io 原子写→目标文件存在且内容逐字节一致；
/// 修复前 exportWriter 缝未装配（导出按钮禁用），"真实写出"无从谈起。
TEST(KinHostServicesAssembly, ExportWriter_RealAtomicWrite_KIN_P4)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-08"}, std::vector<std::string>{"AT-04"});
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::filesystem::path target = dir.path().toStdString();

    const auto writer =
        makeKinExportWriter([target] { return target; });
    const std::string content = "# ird-kin-export\ndata\n";
    EXPECT_TRUE(writer("kin-results.json", content));

    // 回读核对（写出面自证——目标在磁盘上完整，非暂存形态）。
    std::ifstream in(target / "kin-results.json", std::ios::binary);
    ASSERT_TRUE(in.is_open());
    const std::string readBack((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
    EXPECT_EQ(readBack, content);
}

/// 目录代建：deep 子目录不存在时写出仍成功（io 层不代建目录的装配层
/// 补偿——AtomicFile prepare 契约"父目录必须已存在"的宿主侧义务）。
TEST(KinHostServicesAssembly, ExportWriter_CreatesMissingDirectory_KIN_P4)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-08"}, std::vector<std::string>{});
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::filesystem::path deep =
        std::filesystem::path(dir.path().toStdString()) / "a" / "b" / "c";
    const auto writer = makeKinExportWriter([deep] { return deep; });
    EXPECT_TRUE(writer("kin-results.csv", "x,y\n1,2\n"));
    EXPECT_TRUE(std::filesystem::exists(deep / "kin-results.csv"));
}

// =====================================================================
// 求解配置持久化（★ KIN-P4 审查 §4"配置持久化"保存-恢复闭环）
// =====================================================================

/// 保存-恢复闭环：合法配置保存→读回→canonical 解码恢复→逐字段等值。
/// 修复前 configPersist 缝未装配（"用户级持久化通道未装配——编辑仅在
/// 会话生效"），跨会话配置丢失。
TEST(KinHostServicesAssembly, ConfigPersist_SaveRestoreRoundtrip_KIN_P4)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-13"}, std::vector<std::string>{});
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    // 用户级设置路径进程级重定向（gui_test 先例——用例间隔离）。
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       dir.path());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                       QStringLiteral("RobWork"),
                       QStringLiteral("IRD-Studio"));
    settings.clear();

    const auto persist = makeKinConfigPersist(&settings);
    const kin::AnalysisConfiguration saved = makeLegalConfig();
    EXPECT_TRUE(persist(saved));

    const std::optional<kin::AnalysisConfiguration> restored =
        restoreKinConfig(&settings);
    ASSERT_TRUE(restored.has_value());
    // 逐字段等值（值语义面比较——AnalysisConfiguration::operator==）。
    EXPECT_EQ(*restored, saved);
}

/// 损坏恢复诚实回退：字节被篡改→decode 失败→nullopt（调用方回退缺省
/// 基线——绝不把半损坏配置静默钳制成合法，NFR-COR-03）。
TEST(KinHostServicesAssembly, ConfigRestore_CorruptedBytesFallsBack_KIN_P4)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-13"}, std::vector<std::string>{});
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       dir.path());
    QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                       QStringLiteral("RobWork"),
                       QStringLiteral("IRD-Studio"));
    settings.setValue(kKinConfigSettingsKey,
                      QStringLiteral("not-valid-base64-or-canonical!"));
    EXPECT_FALSE(restoreKinConfig(&settings).has_value());

    // 无持久化键（首次使用）＝nullopt——缺省基线回退面。
    settings.remove(kKinConfigSettingsKey);
    EXPECT_FALSE(restoreKinConfig(&settings).has_value());
}
